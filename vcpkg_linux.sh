#!/bin/bash
set -e

mkdir -p build
cd build

#if vcpkg.linux exists
if [ -d "vcpkg.linux" ]; then
	cd vcpkg.linux
else
	git clone https://github.com/Microsoft/vcpkg.git vcpkg.linux
	cd vcpkg.linux
fi

git checkout 2024.08.23
./bootstrap-vcpkg.sh

./vcpkg --recurse install \
	boost-exception:x64-linux \
	boost-program-options:x64-linux \
	boost-stacktrace:x64-linux \
	glfw3:x64-linux \
	glm:x64-linux \
	imgui[core,freetype,glfw-binding,vulkan-binding,docking-experimental]:x64-linux \
	stb:x64-linux \
	tinyobjloader:x64-linux \
	curl:x64-linux \
	tinygltf:x64-linux \
	draco:x64-linux \
	rapidjson:x64-linux \
	fmt:x64-linux \
	cpp-base64:x64-linux
