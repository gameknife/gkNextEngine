#!/bin/bash
set -e

mkdir -p build
cd build

#if vcpkg.linux exists
if [ -d "vcpkg.linux" ]; then
	cd vcpkg.linux
	
	echo "Updating vcpkg..."
	git pull origin master
	./bootstrap-vcpkg.sh
	
	echo "Updating installed packages..."
	./vcpkg update
	./vcpkg upgrade --no-dry-run
	
else
	git clone https://github.com/Microsoft/vcpkg.git vcpkg.linux
	cd vcpkg.linux
	./bootstrap-vcpkg.sh
fi

./vcpkg --recurse install \
	cpptrace:x64-linux \
	cxxopts:x64-linux \
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
	meshoptimizer:x64-linux \
	ktx:x64-linux \
	joltphysics:x64-linux \
	xxhash:x64-linux \
	cpp-base64:x64-linux

