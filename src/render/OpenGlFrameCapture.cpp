#include "render/OpenGlFrameCapture.h"

#include <glad/gl.h>
#include <algorithm>
#include <stdexcept>

namespace ParallelRoam::Render
{
FrameCapture CaptureOpenGlBackBuffer(std::uint64_t id, int width, int height)
{
    if (width <= 0 || height <= 0) throw std::runtime_error("截图尺寸无效");
    FrameCapture result;
    result.Id = id;
    result.Width = width;
    result.Height = height;
    const auto stride = static_cast<std::size_t>(width) * 4;
    result.Rgba.resize(stride * static_cast<std::size_t>(height));
    GLint alignment{}, readBuffer{}, framebuffer{}, packBuffer{}, rowLength{}, skipRows{}, skipPixels{};
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
    glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
    glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
    glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
    glGetIntegerv(GL_READ_BUFFER, &readBuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);

    // 指针必须指向CPU存储；恢复绑定后正常渲染仍沿用原状态
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, result.Rgba.data());
    const GLenum error = glGetError();
    glPixelStorei(GL_PACK_ALIGNMENT, alignment);
    glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
    glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
    glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glReadBuffer(static_cast<GLenum>(readBuffer));
    glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(packBuffer));
    if (error != GL_NO_ERROR) throw std::runtime_error("OpenGL截图回读失败");

    // OpenGL从左下角返回行；图像文件采用左上角，不修改列方向
    for (int row = 0; row < height / 2; ++row)
    {
        const auto first = result.Rgba.begin() + static_cast<std::ptrdiff_t>(row) * static_cast<std::ptrdiff_t>(stride);
        const auto last = result.Rgba.begin() + static_cast<std::ptrdiff_t>(height - 1 - row) * static_cast<std::ptrdiff_t>(stride);
        std::swap_ranges(first, first + static_cast<std::ptrdiff_t>(stride), last);
    }
    return result;
}
}
