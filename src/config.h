#pragma once
#include <iostream>
#include <glad/glad.h>   // 这个得在GLFW之前引入，因为GLFW会使用OpenGL函数指针，而这些指针是由glad加载的
#include <GLFW/glfw3.h>  
// iostream , 控制台输入输出
// glfw3 ， 跨平台窗口库
// glad， OpenGL函数加载库

#include <fstream> // 文件流， 用于读取文件
#include <sstream> // 字符串流， 用于将文件内容读入字符串
#include <string>  // 字符串类

#include "Shader.h"
#include "Mesh.h"

#include "ObjLoader.h"