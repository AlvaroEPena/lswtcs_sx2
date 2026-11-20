// SPDX-License-Identifier: MIT
// Native GLES overlay. Runs on the presentation thread, inside GL ownership.
// Draws one colored-triangle batch and restores the game's GL state afterwards.
#include <switch.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "port_menu.h"
#include "util.h"

#define DRAW_FRAMEBUFFER 0x8CA9
#define READ_FRAMEBUFFER_BINDING 0x8CAA
#define VERTEX_ARRAY_BINDING 0x85B5
#define RASTERIZER_DISCARD 0x8C89
#define UI_WIDTH 1280.0f
#define UI_HEIGHT 720.0f
#define MAX_VERTICES 18000

typedef void (*BindVao)(GLuint);
typedef void (*GenVaos)(GLsizei, GLuint *);
static BindVao bind_vao;
static GenVaos gen_vaos;
static GLuint program, vertex_buffer, vertex_array;
static int initialization_attempted, es3;
static EGLContext owner_context;

typedef struct { GLfloat x, y, r, g, b, a; } Vertex;
static Vertex vertices[MAX_VERTICES];
static unsigned vertex_count;

// Original 5x7 block glyphs, stored as five-bit rows. No font asset needed.
static const uint8_t font[128][7] = {
  ['A']={14,17,17,31,17,17,17}, ['B']={30,17,17,30,17,17,30},
  ['C']={14,17,16,16,16,17,14}, ['D']={30,17,17,17,17,17,30},
  ['E']={31,16,16,30,16,16,31}, ['F']={31,16,16,30,16,16,16},
  ['G']={14,17,16,23,17,17,15}, ['H']={17,17,17,31,17,17,17},
  ['I']={14,4,4,4,4,4,14}, ['J']={7,2,2,2,18,18,12},
  ['K']={17,18,20,24,20,18,17}, ['L']={16,16,16,16,16,16,31},
  ['M']={17,27,21,21,17,17,17}, ['N']={17,25,21,19,17,17,17},
  ['O']={14,17,17,17,17,17,14}, ['P']={30,17,17,30,16,16,16},
  ['Q']={14,17,17,17,21,18,13}, ['R']={30,17,17,30,20,18,17},
  ['S']={15,16,16,14,1,1,30}, ['T']={31,4,4,4,4,4,4},
  ['U']={17,17,17,17,17,17,14}, ['V']={17,17,17,17,17,10,4},
  ['W']={17,17,17,21,21,21,10}, ['X']={17,17,10,4,10,17,17},
  ['Y']={17,17,10,4,4,4,4}, ['Z']={31,1,2,4,8,16,31},
  ['0']={14,17,19,21,25,17,14}, ['1']={4,12,4,4,4,4,14},
  ['2']={14,17,1,2,4,8,31}, ['3']={30,1,1,14,1,1,30},
  ['4']={2,6,10,18,31,2,2}, ['5']={31,16,16,30,1,1,30},
  ['6']={14,16,16,30,17,17,14}, ['7']={31,1,2,4,8,8,8},
  ['8']={14,17,17,14,17,17,14}, ['9']={14,17,17,15,1,1,14},
  ['.']={0,0,0,0,0,6,6}, [':']={0,6,6,0,6,6,0},
  ['-']={0,0,0,31,0,0,0}, ['+']={0,4,4,31,4,4,0},
  ['/']={1,2,2,4,8,8,16}, ['(']={2,4,8,8,8,4,2},
  [')']={8,4,2,2,2,4,8}, ['?']={14,17,1,2,4,0,4},
};

static void rectangle(float x, float y, float w, float h, uint32_t rgba) {
  if (vertex_count + 6 > MAX_VERTICES || w <= 0 || h <= 0) return;
  const float l = 2*x/UI_WIDTH-1, r = 2*(x+w)/UI_WIDTH-1;
  const float t = 1-2*y/UI_HEIGHT, b = 1-2*(y+h)/UI_HEIGHT;
  const float red = ((rgba >> 24) & 255)/255.f;
  const float green = ((rgba >> 16) & 255)/255.f;
  const float blue = ((rgba >> 8) & 255)/255.f, alpha = (rgba & 255)/255.f;
  const Vertex quad[] = {{l,t,red,green,blue,alpha},{l,b,red,green,blue,alpha},
    {r,t,red,green,blue,alpha},{r,t,red,green,blue,alpha},
    {l,b,red,green,blue,alpha},{r,b,red,green,blue,alpha}};
  memcpy(vertices + vertex_count, quad, sizeof(quad));
  vertex_count += 6;
}

static void text(float x, float y, int scale, const char *str, uint32_t rgba) {
  for (; *str; ++str, x += 6*scale) {
    unsigned char c = (unsigned char)*str;
    if (c >= 'a' && c <= 'z') c -= 'a'-'A';
    if (c >= 128) c = '?';
    for (int row = 0; row < 7; ++row) {
      int col = 0;
      while (col < 5) {
        if (!(font[c][row] & (1 << (4-col)))) { ++col; continue; }
        const int first = col++;
        while (col < 5 && (font[c][row] & (1 << (4-col)))) ++col;
        rectangle(x+first*scale, y+row*scale, (col-first)*scale, scale, rgba);
      }
    }
  }
}

static GLuint compile(GLenum type, const char *source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512]; GLsizei length = 0;
    glGetShaderInfoLog(shader, sizeof(log), &length, log);
    debugPrintf("port overlay shader: %.*s\n", (int)length, log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static int initialize(void) {
  if (initialization_attempted) return program != 0;
  initialization_attempted = 1;
  const char *version = (const char *)glGetString(GL_VERSION);
  es3 = version && strstr(version, "OpenGL ES 3") != NULL;
  GLuint vs = compile(GL_VERTEX_SHADER,
    "#version 100\nattribute vec2 a_position; attribute vec4 a_color;\n"
    "varying lowp vec4 color; void main(){color=a_color;gl_Position=vec4(a_position,0.0,1.0);}");
  GLuint fs = compile(GL_FRAGMENT_SHADER,
    "#version 100\nprecision mediump float; varying lowp vec4 color;\n"
    "void main(){gl_FragColor=color;}");
  if (!vs || !fs) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return 0;
  }
  program = glCreateProgram();
  glAttachShader(program, vs); glAttachShader(program, fs);
  glBindAttribLocation(program, 0, "a_position");
  glBindAttribLocation(program, 1, "a_color");
  glLinkProgram(program);
  glDeleteShader(vs); glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[512]; GLsizei length = 0;
    glGetProgramInfoLog(program, sizeof(log), &length, log);
    debugPrintf("port overlay link: %.*s\n", (int)length, log);
    glDeleteProgram(program); program = 0;
    return 0;
  }
  glGenBuffers(1, &vertex_buffer);
  bind_vao = (BindVao)eglGetProcAddress("glBindVertexArray");
  gen_vaos = (GenVaos)eglGetProcAddress("glGenVertexArrays");
  if (!bind_vao || !gen_vaos) {
    bind_vao = (BindVao)eglGetProcAddress("glBindVertexArrayOES");
    gen_vaos = (GenVaos)eglGetProcAddress("glGenVertexArraysOES");
  }
  if (bind_vao && gen_vaos) gen_vaos(1, &vertex_array);
  if (!vertex_buffer) {
    glDeleteProgram(program); program = 0;
    return 0;
  }
  debugPrintf("port overlay initialized: %s, VAO=%u\n", version ? version : "unknown", vertex_array);
  return 1;
}

typedef struct {
  GLint enabled, size, type, normalized, stride, buffer;
  void *pointer;
  GLfloat value[4];
} AttributeState;

static void save_attribute(unsigned index, AttributeState *s, int pointers) {
  glGetVertexAttribfv(index, GL_CURRENT_VERTEX_ATTRIB, s->value);
  if (!pointers) return;
  glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &s->enabled);
  glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &s->size);
  glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &s->type);
  glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &s->normalized);
  glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &s->stride);
  glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &s->buffer);
  glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER, &s->pointer);
}

static void restore_attribute(unsigned index, const AttributeState *s, int pointers) {
  if (pointers) {
    glBindBuffer(GL_ARRAY_BUFFER, s->buffer);
    glVertexAttribPointer(index, s->size, s->type, s->normalized, s->stride, s->pointer);
    if (s->enabled) glEnableVertexAttribArray(index); else glDisableVertexAttribArray(index);
  }
  glVertexAttrib4fv(index, s->value);
}

void port_overlay_draw(int width, int height) {
  PortMenuSnapshot ui;
  port_menu_snapshot(&ui);
  if ((!ui.open && !ui.show_fps) || width <= 0 || height <= 0) return;
  const EGLContext context = eglGetCurrentContext();
  if (context == EGL_NO_CONTEXT) return;
  if (owner_context != context) {
    owner_context = context;
    initialization_attempted = 0;
    program = vertex_buffer = vertex_array = 0;
  }
  if (!initialize()) return;

  vertex_count = 0;
  char fps[32];
  if (ui.fps_tenths > 0) snprintf(fps, sizeof(fps), "FPS %d.%d", ui.fps_tenths/10, ui.fps_tenths%10);
  else snprintf(fps, sizeof(fps), "FPS --.-");
  if (ui.show_fps) {
    const int scale = ui.fps_size >= 0 && ui.fps_size <= 2 ? ui.fps_size + 2 : 3;
    const int hud_width = (int)strlen(fps)*6*scale + 26, hud_height = 7*scale + 22;
    rectangle(16,16,hud_width,hud_height,0x101826E8);
    rectangle(16,16,4,hud_height,0xF4C95DFF);
    text(29,27,scale,fps,0xFFF2C7FF);
  }
  if (ui.open) {
    rectangle(0,0,1280,720,0x00000070);
    rectangle(320,148,640,424,0x101826F5);
    rectangle(320,148,640,5,0xF4C95DFF);
    text(355,178,4,"PORT MENU",0xFFFFFFFF);
    text(356,220,2,"LEGO STAR WARS / SWITCH 2",0xA6B3C9FF);
    rectangle(352,264+ui.selected*60,576,52,0x2E415DFF);
    text(371,280,3,"FPS COUNTER",0xFFFFFFFF);
    text(840,280,3,ui.show_fps ? "ON" : "OFF",ui.show_fps ? 0xF4C95DFF : 0xABB8CCFF);
    text(371,340,3,"FPS SIZE",0xFFFFFFFF);
    const char *sizes[] = {"SMALL","MEDIUM","LARGE"};
    const char *size_label = sizes[ui.fps_size >= 0 && ui.fps_size <= 2 ? ui.fps_size : 1];
    text(910-(int)strlen(size_label)*18,340,3,size_label,0xF4C95DFF);
    text(371,400,3,"BACK TO GAME",0xFFFFFFFF);
    text(356,466,2,"UP/DOWN SELECT   A CONFIRM   B CLOSE",0xDCE5F3FF);
    text(356,490,2,"L+R+DOWN TOGGLE MENU",0xA6B3C9FF);
    const uint64_t now = armGetSystemTick(), freq = armGetSystemTickFreq();
    if (ui.saved_at && now >= ui.saved_at && now-ui.saved_at < freq*2)
      text(356,536,2,ui.save_ok ? "SETTING SAVED" : "COULD NOT SAVE SETTING",ui.save_ok ? 0x98E6A5FF : 0xFF9B8EFF);
    else text(356,536,2,"GAME CONTINUES IN BACKGROUND",0xA6B3C9FF);
  }

  GLint old_program, old_buffer, old_vao = 0, old_fbo, viewport[4];
  GLint src_rgb, dst_rgb, src_alpha, dst_alpha, eq_rgb, eq_alpha;
  GLboolean color_mask[4];
  glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_buffer);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
  glGetIntegerv(GL_VIEWPORT, viewport);
  glGetIntegerv(GL_BLEND_SRC_RGB, &src_rgb); glGetIntegerv(GL_BLEND_DST_RGB, &dst_rgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_alpha); glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_alpha);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &eq_rgb); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &eq_alpha);
  glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
  const GLenum capabilities[] = {GL_BLEND,GL_DEPTH_TEST,GL_CULL_FACE,GL_SCISSOR_TEST,
    GL_STENCIL_TEST,GL_SAMPLE_ALPHA_TO_COVERAGE,GL_SAMPLE_COVERAGE};
  GLboolean enabled[sizeof(capabilities)/sizeof(capabilities[0])];
  for (unsigned i = 0; i < sizeof(enabled)/sizeof(enabled[0]); ++i) enabled[i] = glIsEnabled(capabilities[i]);
  const GLboolean discard = es3 ? glIsEnabled(RASTERIZER_DISCARD) : GL_FALSE;
  AttributeState attributes[2];
  save_attribute(0, &attributes[0], !vertex_array);
  save_attribute(1, &attributes[1], !vertex_array);
  if (vertex_array) {
    glGetIntegerv(VERTEX_ARRAY_BINDING, &old_vao);
    bind_vao(vertex_array);
  }
  glBindFramebuffer(es3 ? DRAW_FRAMEBUFFER : GL_FRAMEBUFFER, 0);
  glViewport(0,0,width,height);
  for (unsigned i = 0; i < sizeof(enabled)/sizeof(enabled[0]); ++i) glDisable(capabilities[i]);
  if (es3) glDisable(RASTERIZER_DISCARD);
  glEnable(GL_BLEND);
  glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  glUseProgram(program);
  glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
  glBufferData(GL_ARRAY_BUFFER, vertex_count*sizeof(Vertex), vertices, GL_STREAM_DRAW);
  glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),(const void *)offsetof(Vertex,x));
  glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),(const void *)offsetof(Vertex,r));
  glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
  glDrawArrays(GL_TRIANGLES,0,vertex_count);

  if (vertex_array) bind_vao(old_vao);
  restore_attribute(0, &attributes[0], !vertex_array);
  restore_attribute(1, &attributes[1], !vertex_array);
  glBindBuffer(GL_ARRAY_BUFFER, old_buffer);
  glUseProgram(old_program);
  glBindFramebuffer(es3 ? DRAW_FRAMEBUFFER : GL_FRAMEBUFFER, old_fbo);
  glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
  glBlendFuncSeparate(src_rgb,dst_rgb,src_alpha,dst_alpha);
  glBlendEquationSeparate(eq_rgb,eq_alpha);
  glColorMask(color_mask[0],color_mask[1],color_mask[2],color_mask[3]);
  for (unsigned i = 0; i < sizeof(enabled)/sizeof(enabled[0]); ++i) {
    if (enabled[i]) glEnable(capabilities[i]); else glDisable(capabilities[i]);
  }
  if (es3 && discard) glEnable(RASTERIZER_DISCARD);
}
