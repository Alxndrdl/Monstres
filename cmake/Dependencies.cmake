# Third-party code, downloaded at configure time and checked by hash.
include(FetchContent)

# cgltf: glTF 2.0 loader, a single C header (MIT). Used by src/render/models.cpp.
FetchContent_Declare(cgltf
  URL https://raw.githubusercontent.com/jkuhlmann/cgltf/v1.14/cgltf.h
  URL_HASH SHA256=19db3bdf85f6aa991a3b9d056641bcb39a2ef11cd5211e7a9f6b6c307d9ebc64
  DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(cgltf)
