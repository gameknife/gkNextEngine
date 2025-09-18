#!/bin/bash
set -e

mkdir -p build
cd build

#if vcpkg.android exists
if [ -d "vcpkg.android" ]; then
	cd vcpkg.android
	
	echo "Updating vcpkg..."
	git pull origin master
	./bootstrap-vcpkg.sh
	
	echo "Updating installed packages..."
	./vcpkg update
	./vcpkg upgrade --no-dry-run
	
else
	git clone https://github.com/Microsoft/vcpkg.git vcpkg.android
	cd vcpkg.android
	./bootstrap-vcpkg.sh
fi

#replace the triplets/arm64-android.cmake file with ours
cp -f ../../android/custom-triplets/arm64-android.cmake ./triplets/arm64-android.cmake

./vcpkg --recurse install \
	cxxopts:arm64-android \
	glm:arm64-android \
	imgui[core,freetype,android-binding,vulkan-binding,docking-experimental]:arm64-android \
	stb:arm64-android \
	tinyobjloader:arm64-android \
	curl:arm64-android \
	tinygltf:arm64-android \
	draco:arm64-android \
	fmt:arm64-android \
	meshoptimizer:arm64-android \
	ktx:arm64-android \
	joltphysics:arm64-android \
	xxhash:arm64-android \
	cpp-base64:arm64-android
