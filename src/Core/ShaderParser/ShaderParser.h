// 用于解析Shader，将规范的Shader转换为GPU需要的Shader信息
// 作为Core当中的工具类函数库，不依赖任何现有模块
#pragma once

// 必要头文件
#include "iostream"
#include "map"
#include "fstream"
#include "sstream"
#include "string"


namespace ShaderParser {
    // Shader状态，这是定义的Shader格式，Shader中最上面应该是通用的 Properties，用于所有Pass内，然后也是后续Material的属性
    // 然后是Common，用于所有Pass内，包含变量以及函数，
    // 然后是Pass，Pass后双引号内包含Pass的名称，然后Pass内包含Vertex和Fragment
    // Vertex和Fragment中完整包含 Properties、Common的内容，GLSL版本默认 460
    enum ShaderState { None, Properties, Common, Pass };
    enum PassState { pNone, Vertex, Fragment };
    // Pass数据
    struct PassData {
    std::string vertex;
    std::string fragment;
    };

    // 获取路径下的文件内容
    std::string getPathContent(const std::string& path);

    // 获取Shader内容
    bool ParserShaderFromPath(const std::string& filePath,
                         std::map<std::string, PassData>& shaderPasses,
                         std::map<std::string, std::string>& shaderSource);

}