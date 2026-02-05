you need 32 bit grpc to build this, yes i ship the entire grpc runtime in the `.so`, no i don't care

```
CFLAGS="-m32 -O2 -fPIC"
CXXFLAGS="-m32 -O2 -fPIC"
cmake -B build-32 \
    -DCMAKE_INSTALL_PREFIX=/opt/p2-grpc32 \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DgRPC_INSTALL=ON \
    -DgRPC_BUILD_TESTS=OFF \
    -DgRPC_BUILD_CSHARP_EXT=OFF \
    -DgRPC_BUILD_GRPC_CSHARP_PLUGIN=OFF \
    -DgRPC_BUILD_GRPC_NODE_PLUGIN=OFF \
    -DgRPC_BUILD_GRPC_PYTHON_PLUGIN=ON \
    -DgRPC_ABSL_PROVIDER=module \
    -DgRPC_PROTOBUF_PROVIDER=module \
    -DgRPC_RE2_PROVIDER=module \
    -DgRPC_SSL_PROVIDER=none \
    -DCMAKE_C_FLAGS="$CFLAGS" \
    -DCMAKE_CXX_FLAGS="$CXXFLAGS"
```

game launch options: `gamescope -w 640 -h 480 -W 640 -H 480 -b -- %command% -dev -insecure -console -novid -vulkan -sw +engine_no_focus_sleep 0 -nomousegrab`

needs `gamescope` (available on arch cuz steamos, not sure about other operating systems)
