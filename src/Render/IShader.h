// Shader抽象接口，目的是解耦底层API（OpenGL、Vulkan、DirectX等）和上层渲染逻辑。上层渲染逻辑只需要关心Shader的接口，而不需要关心底层API的具体实现。
#pragma once
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

class IShader{
    public:
        virtual ~IShader() = default;
        virtual bool BuildFromFiles(const std::string& vertexPath, const std::string& fragmentPath) = 0; // 从文件构建着色器
        virtual unsigned int GetID() const = 0; // 获取着色器程序ID
        virtual void Use() = 0; // 使用着色器程序
        virtual void SetMatrix(const glm::mat4& ModeMatrix,const glm::mat4& ViewMatrix,const glm::mat4& ProjectionMatrix) = 0; // 设置MVP矩阵Uniform
        virtual void SetLight(const glm::vec3& lightPos, const glm::vec3& lightColor) = 0; // 设置灯光Uniform
        virtual void SetCamera(const glm::vec3& cameraPos) = 0; // 设置摄像机相关Uniform

        // Shadow Pass 嵌入式
        virtual void SetMat4(const std::string& name, const glm::mat4& value) = 0;
        virtual void SetInt (const std::string& name, int value) = 0;
        // 屏幕空间 Pass 需要知道"屏幕有多大"才能把 gl_FragCoord 换算成 [0,1] 的 UV。
        // ★ 必须传屏幕尺寸，不能拿 textureSize(屏幕空间纹理) 当分母：
        //   两者只有在"屏幕空间纹理是全分辨率"时才相等；一旦降分辨率（比如半分辨率），
        //   textureSize 会变成一半，UV 就会变成 0~2 → 画面被缩小贴到左下角。
        virtual void SetVec2(const std::string& name, const glm::vec2& value) = 0;
        virtual void SetVec3(const std::string& name, const glm::vec3& value) = 0;
};