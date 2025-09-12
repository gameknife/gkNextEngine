#!/bin/bash
set -e

mkdir -p build
cd build

#if vcpkg.mingw exists
if [ -d "vcpkg.mingw" ]; then
	cd vcpkg.mingw
	
	echo "Updating vcpkg..."
	git pull origin master
	./bootstrap-vcpkg.sh
	
	echo "Updating installed packages..."
	./vcpkg update
	./vcpkg upgrade --no-dry-run
	
else
	git clone https://github.com/Microsoft/vcpkg.git vcpkg.mingw
	cd vcpkg.mingw
	./bootstrap-vcpkg.sh
fi

./vcpkg --recurse install \
	cpptrace:x64-mingw-static \
	cxxopts:x64-mingw-static \
	glfw3:x64-mingw-static \
	glm:x64-mingw-static \
	imgui[core,freetype,glfw-binding,vulkan-binding,docking-experimental]:x64-mingw-static \
	stb:x64-mingw-static \
	tinyobjloader:x64-mingw-static \
	curl:x64-mingw-static \
	tinygltf:x64-mingw-static \
	draco:x64-mingw-static \
	rapidjson:x64-mingw-static \
	fmt:x64-mingw-static \
	meshoptimizer:x64-mingw-static \
	ktx:x64-mingw-static \
	joltphysics:x64-mingw-static \
	xxhash:x64-mingw-static \
	cpp-base64:x64-mingw-static
