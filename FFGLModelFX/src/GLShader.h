#pragma once
#include <string>
#include <vector>
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

class GLShader {
public:
  GLShader();
  ~GLShader();

  bool loadFromFiles(const std::string& vsPath, const std::string& fsPath, std::string& log);
  void use() const;
  GLint uniformLoc(const char* name) const;
  GLuint program() const { return program_; }

private:
  GLuint program_ = 0;
  static bool compile(GLuint shader, const std::string& src, std::string& log);
  static bool readFile(const std::string& path, std::string& out);
};