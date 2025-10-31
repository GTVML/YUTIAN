#include "GLShader.h"
#include <fstream>
#include <sstream>

GLShader::GLShader(){}
GLShader::~GLShader(){ if(program_) glDeleteProgram(program_); }

bool GLShader::readFile(const std::string& path, std::string& out){
  std::ifstream f(path, std::ios::in);
  if(!f) return false;
  std::ostringstream ss; ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool GLShader::compile(GLuint shader, const std::string& src, std::string& log){
  const char* c = src.c_str();
  glShaderSource(shader, 1, &c, nullptr);
  glCompileShader(shader);
  GLint ok=0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if(!ok){
    GLint len=0; glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
    std::vector<char> buf(len);
    glGetShaderInfoLog(shader, len, nullptr, buf.data());
    log.assign(buf.begin(), buf.end());
    return false;
  }
  return true;
}

bool GLShader::loadFromFiles(const std::string& vsPath, const std::string& fsPath, std::string& log){
  std::string vss, fss;
  if(!readFile(vsPath, vss)) { log = "Failed to read VS: " + vsPath; return false; }
  if(!readFile(fsPath, fss)) { log = "Failed to read FS: " + fsPath; return false; }

  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  std::string vlog, flog;
  if(!compile(vs, vss, vlog)) { log = "VS compile error:\n" + vlog; glDeleteShader(vs); glDeleteShader(fs); return false; }
  if(!compile(fs, fss, flog)) { log = "FS compile error:\n" + flog; glDeleteShader(vs); glDeleteShader(fs); return false; }

  program_ = glCreateProgram();
  glAttachShader(program_, vs);
  glAttachShader(program_, fs);
  glLinkProgram(program_);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint ok=0; glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if(!ok){
    GLint len=0; glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &len);
    std::vector<char> buf(len);
    glGetProgramInfoLog(program_, len, nullptr, buf.data());
    log.assign(buf.begin(), buf.end());
    glDeleteProgram(program_); program_ = 0; return false;
  }
  return true;
}

void GLShader::use() const{ glUseProgram(program_); }
GLint GLShader::uniformLoc(const char* name) const{ return glGetUniformLocation(program_, name); }
