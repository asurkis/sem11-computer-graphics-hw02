#include "routine.hpp"
#include <map>

#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_common.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

#include <tiny_gltf.h>

void loadModel(tinygltf::Model &model, const std::string &path) {
    std::string err;
    std::string warn;
    tinygltf::TinyGLTF loader;
    bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);

    std::cout << warn << '\n' << err << '\n';

    if (!ret) {
        throw std::runtime_error("Could not load model");
    }
}

void bindBufferViews(std::map<int, GLuint> &vbos,
                     const tinygltf::Model &model) {
    for (int i = 0; i < model.bufferViews.size(); ++i) {
        auto &bufferView = model.bufferViews[i];
        if (bufferView.target == 0) {
            continue;
        }

        auto &buffer = model.buffers[bufferView.buffer];
        GLuint vbo = 0;
        glGenBuffers(1, &vbo);
        vbos[i] = vbo;

        RaiiBindBuffer _bind(bufferView.target, vbo);
        glBufferData(bufferView.target, bufferView.byteLength,
                     &buffer.data[bufferView.byteOffset], GL_STATIC_DRAW);
    }

    // TODO: textures
}

void bindMesh(std::map<int, GLuint> &vbos, const tinygltf::Model &model,
              const tinygltf::Mesh &mesh) {
    for (auto &primitive : mesh.primitives) {
        auto &idxAccessor = model.accessors[primitive.indices];

        for (auto &[name, accessorId] : primitive.attributes) {
            int vaa = -1;
            if (name == "POSITION") {
                vaa = 0;
            } else if (name == "NORMAL") {
                vaa = 1;
            } else if (name == "TEXCOORD_0") {
                vaa = 2;
            } else {
                std::cerr << "Unknown parameter " << name << '\n';
                continue;
            }

            auto &accessor = model.accessors[accessorId];

            int size = 1;
            if (accessor.type != TINYGLTF_TYPE_SCALAR) {
                size = accessor.type;
            }

            int byteStride =
                accessor.ByteStride(model.bufferViews[accessor.bufferView]);

            glBindBuffer(GL_ARRAY_BUFFER, vbos[accessor.bufferView]);
            glEnableVertexAttribArray(vaa);
            glVertexAttribPointer(
                vaa, size, accessor.componentType,
                accessor.normalized ? GL_TRUE : GL_FALSE, byteStride,
                static_cast<char *>(nullptr) + accessor.byteOffset);
        }
    }
}

void bindModelNode(std::map<int, GLuint> &vbos, const tinygltf::Model &model,
                   const tinygltf::Node &node) {
    if (0 <= node.mesh && node.mesh < model.meshes.size()) {
        auto &mesh = model.meshes[node.mesh];
        bindMesh(vbos, model, mesh);
    }
    for (auto childId : node.children) {
        auto &child = model.nodes[childId];
        bindModelNode(vbos, model, child);
    }
}

std::pair<GLuint, std::map<int, GLuint>>
bindModel(const tinygltf::Model &model) {
    std::map<int, GLuint> vbos;
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    {
        RaiiBindVao _bind(vao);
        auto &scene = model.scenes[model.defaultScene];
        bindBufferViews(vbos, model);
        for (auto nodeId : scene.nodes) {
            auto &node = model.nodes[nodeId];
            bindModelNode(vbos, model, node);
        }
    }

    for (auto it = vbos.begin(); it != vbos.end();) {
        auto &bufferView = model.bufferViews[it->first];
        if (bufferView.target == GL_ELEMENT_ARRAY_BUFFER) {
            ++it;
        } else {
            glDeleteBuffers(1, &it->second);
            vbos.erase(it++);
        }
    }
    return {vao, std::move(vbos)};
}

void drawModelMeshes(const std::map<int, GLuint> &ebos,
                     const tinygltf::Model &model, const tinygltf::Mesh &mesh) {
    for (auto &primitive : mesh.primitives) {
        auto &accessor = model.accessors[primitive.indices];
        RaiiBindBuffer _bind(GL_ELEMENT_ARRAY_BUFFER,
                             ebos.at(accessor.bufferView));
        glDrawElements(primitive.mode, accessor.count, accessor.componentType,
                       static_cast<char *>(nullptr) + accessor.byteOffset);
    }
}

void drawModelNodes(const std::map<int, GLuint> &ebos,
                    const tinygltf::Model &model, const tinygltf::Node &node) {
    auto &mesh = model.meshes[node.mesh];
    drawModelMeshes(ebos, model, mesh);
    for (auto &childId : node.children) {
        auto &child = model.nodes[childId];
        drawModelNodes(ebos, model, child);
    }
}

void drawModel(GLuint vao, const std::map<int, GLuint> &ebos,
               const tinygltf::Model &model) {
    RaiiBindVao _bind(vao);
    auto &scene = model.scenes[model.defaultScene];
    for (auto &nodeId : scene.nodes) {
        auto &node = model.nodes[nodeId];
        drawModelNodes(ebos, model, node);
    }
}

int main() {
    RaiiContext _context;

    Shader shaderVert(GL_VERTEX_SHADER, "main.vert");
    Shader shaderFrag(GL_FRAGMENT_SHADER, "main.frag");
    ShaderProgram program = ShaderProgram(shaderVert.get(), shaderFrag.get());

    GLuint uniformModel = glGetUniformLocation(program.get(), "matModel");
    GLuint uniformView = glGetUniformLocation(program.get(), "matView");
    GLuint uniformProj = glGetUniformLocation(program.get(), "matProj");

    tinygltf::Model model;
    loadModel(model, "model.gltf");
    auto [vao, idxBuffers] = bindModel(model);

    while (!glfwWindowShouldClose(window)) {
        RaiiFrame _frame;

        int width = 0, height = 0;
        glfwGetWindowSize(window, &width, &height);
        glViewport(0, 0, width, height);

        glClearColor(1.0f, 0.75f, 0.5f, 1.0f);
        glClearDepth(0.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        double time = glfwGetTime();
        double angle = 0.1 * time;
        double cycle = glm::fract(0.5 * time);
        double rotAngle =
            2.0 * glm::pi<double>() * glm::smoothstep(0.0, 1.0, cycle);
        glm::mat4 matModel = glm::rotate(
            glm::mat4(1.0f), static_cast<float>(rotAngle), {1.0f, 0.0f, 0.0f});

        glm::mat4 matView = glm::lookAt(
            glm::vec3{5.0 * glm::cos(angle), 5.0 * glm::sin(angle), 2.0f},
            glm::vec3{}, glm::vec3{0.0f, 0.0f, 1.0f});
        glm::mat4 matProj = glm::perspective(
            glm::radians(45.0f), 1.0f * width / height, 100.0f, 0.001f);

        RaiiUseProgram _bind1(program.get());
        glUniformMatrix4fv(uniformModel, 1, GL_FALSE,
                           reinterpret_cast<GLfloat *>(&matModel));
        glUniformMatrix4fv(uniformView, 1, GL_FALSE,
                           reinterpret_cast<GLfloat *>(&matView));
        glUniformMatrix4fv(uniformProj, 1, GL_FALSE,
                           reinterpret_cast<GLfloat *>(&matProj));

        glEnable(GL_MULTISAMPLE);
        glEnable(GL_DEPTH_TEST);
        // glEnable(GL_CULL_FACE);

        glDepthFunc(GL_GREATER);
        drawModel(vao, idxBuffers, model);

        // glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_MULTISAMPLE);
    }

    glDeleteVertexArrays(1, &vao);
    // glDeleteBuffers(2, buffers);
    for (auto [_pos, idx] : idxBuffers) {
        glDeleteBuffers(1, &idx);
    }

    return 0;
}
