#include "ShaderParser.h"

// 匿名空间，仅在当前文件中使用
namespace {
// 统计一行中 { 和 } 的净变化量
int countBraceDelta(const std::string& line) {
    int delta = 0;
    for (char c : line) {
        if (c == '{') delta++;
        else if (c == '}') delta--;
    }
    return delta;
}
// 获取Shader中#include的文件的内容
std::string getIncludeContent(const std::string& line) {
    if (line.find("#include") == std::string::npos) {
        return "";
    }
    // 找第一个引号
    size_t q1 = line.find('"');
    if (q1 == std::string::npos) return "";
    // 找第二个引号
    size_t q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    std::string includePath = line.substr(q1 + 1, q2 - q1 - 1);
    return ShaderParser::getPathContent(PROJECT_SOURCE_DIR + includePath);
}
}

namespace ShaderParser {
    // 获取路径下的文件内容
    std::string getPathContent(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cout << "open file failed:" << path << std::endl;
            return "";
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        return buffer.str();
    }

    // 获取Shader内容
    bool ParserShaderFromPath(const std::string& filePath,
                         std::map<std::string, PassData>& shaderPasses,
                         std::map<std::string, std::string>& shaderSource) {

        std::string GLSL_VERSION = "#version 460 core\n";

        std::ifstream file(filePath);
        if (!file.is_open()) {
            std::cout << "[ShaderParser] Failed to parse shader asset:" << filePath << std::endl;
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        std::istringstream stream(buffer.str());
        std::string line;

        ShaderState shaderState = None;
        PassState passState = pNone;
        std::string currentPass;
        int depth = 0;

        while (std::getline(stream, line)) {

            // 去掉行首空白，得到"逻辑行"
            size_t firstNonSpace = line.find_first_not_of(" \t");  // 行首第一个非空白字符的位置
            std::string trimmed = (firstNonSpace != std::string::npos) // 从第一个非空白字符开始的字符
                ? line.substr(firstNonSpace) : "";

            // ===== 状态转移检测（匹配到的行包含 '{'，depth 设为 1）=====
            if (trimmed.find("Properties") != std::string::npos && trimmed.find('{') != std::string::npos) {
                shaderState = Properties;
                depth = 1;  // 这一行有 '{'
                continue;
            }
            if (trimmed.find("Common") != std::string::npos && trimmed.find('{') != std::string::npos) {
                shaderState = Common;
                depth = 1;
                continue;
            }
            if (trimmed.find("Pass \"") != std::string::npos) {
                shaderState = Pass;
                passState = pNone;
                depth = 1;  // 这一行有 '{'

                // 寻找两个引号之间的内容，即Pass 名
                size_t q1 = trimmed.find('"');
                size_t q2 = trimmed.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    currentPass = trimmed.substr(q1 + 1, q2 - q1 - 1);
                } else {
                    std::cout << "find pass name failed, skip" << std::endl;
                    shaderState = None;
                    depth = 0;
                }
                continue;
            }
            if (shaderState == Pass) {
                if (trimmed.find("Vertex") != std::string::npos && trimmed.find('{') != std::string::npos) {
                    passState = Vertex;
                    depth = 2;  // Pass 的 '{' + Vertex 的 '{'
                    continue;
                }
                if (trimmed.find("Fragment") != std::string::npos && trimmed.find('{') != std::string::npos) {
                    passState = Fragment;
                    depth = 2;  // Pass 的 '{' + Fragment 的 '{'
                    continue;
                }
            }

            // ===== 统一更新 depth（处理当前行里的 { 和 }）=====
            depth += countBraceDelta(trimmed);

            // ===== 内容收集 =====
            if (shaderState == Properties) {
                if (depth <= 0) { shaderState = None; continue; }
                shaderSource["Properties"] += trimmed + "\n";
            }
            else if (shaderState == Common) {
                if (depth <= 0) { shaderState = None; continue; }
                shaderSource["Common"] += trimmed + "\n";
            }
            else if (shaderState == Pass) {
                if (depth <= 0) { shaderState = None; passState = pNone; continue; }

                // 处理 include
                std::string includeContent = getIncludeContent(trimmed);

                if (passState == Vertex) {
                    if (depth <= 1) { passState = pNone; continue; }  // 回到 Pass 层级
                    // 初始化，将 Properties 和 Common 加入
                    if (shaderPasses[currentPass].vertex.empty()) {
                        shaderPasses[currentPass].vertex = GLSL_VERSION + shaderSource["Properties"] + "\n" + shaderSource["Common"] + "\n";
                    }

                    // 如果includeContent不为空，就加入includeContent，否则加入trimmed
                    if (!includeContent.empty()) {
                        shaderPasses[currentPass].vertex += includeContent + "\n";
                    }else{
                        shaderPasses[currentPass].vertex += trimmed + "\n";
                    }
                }
                else if (passState == Fragment) {
                    if (depth <= 1) { passState = pNone; continue; }  // 回到 Pass 层级
                    // 初始化，将 Properties 和 Common 加入
                    if (shaderPasses[currentPass].fragment.empty()) {
                        shaderPasses[currentPass].fragment = GLSL_VERSION + shaderSource["Properties"] + "\n" + shaderSource["Common"] + "\n";
                    }
                    if (!includeContent.empty()) {
                        shaderPasses[currentPass].fragment += includeContent + "\n";
                    }else{
                        shaderPasses[currentPass].fragment += trimmed + "\n";
                    }
                }
            }
        }
        return true;
    }
}