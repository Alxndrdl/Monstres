# Shared compile settings, applied with monstres_set_options(<target>).

function(monstres_set_options target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
  # No fused multiply-add: keeps float results identical natively and in
  # WebAssembly, so game replays give bit-identical states everywhere.
  target_compile_options(${target} PRIVATE -ffp-contract=off)
  if(MONSTRES_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
  if(MONSTRES_SANITIZERS AND NOT EMSCRIPTEN)
    # -O1: the sanitizers' own advice; at -O0 the tests (generating worlds) were several times slower.
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -O1)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
