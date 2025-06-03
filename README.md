# Help for Racoon

Welcome to the help page for **Racoon**, a C++ file manager and image viewer built with SDL2, libwebp, and libavif. This document guides you on getting support and troubleshooting common issues.

## Getting Help

If you encounter issues or have questions, please follow these steps:

1. **Check the Documentation**:
   - Review the [README.md](README.md) for setup, build instructions, and supported image formats (JPEG, PNG, BMP, WebP, AVIF, etc.).
   - Ensure you’ve followed the installation steps for `vcpkg` and copied required DLLs.

2. **Search Existing Issues**:
   - Visit the [Issues](https://github.com/codeboxqc/Racoon/issues) tab to check if your problem has been reported.
   - Use the search bar to find related topics (e.g., "PNG loading", "SDL2_image").

3. **Open a New Issue**:
   - If your issue isn’t addressed, create a new issue:
     - Go to [New Issue](https://github.com/codeboxqc/Racoon/issues/new).
     - Provide a clear title (e.g., "PNG files fail to load in image viewer").
     - Include:
       - **Description**: What’s happening? (e.g., "Double-clicking test.png shows no image.")
       - **Steps to Reproduce**: List actions (e.g., "Build in Release, place test.png in output folder, double-click").
       - **Environment**: OS, Visual Studio version, `vcpkg` version, SDL2_image version.
       - **Logs**: Attach `sdl_error.log` or relevant error messages.
       - **Screenshots**: If applicable.
     - Tag with labels like `bug` or `question`.

4. **Contact the Maintainer**:
   - For urgent or private issues, contact the maintainer via GitHub (codeboxqc).
   - Response times may vary, but we aim to reply within 48 hours.

## Common Issues and Troubleshooting

### 1. PNG or BMP Files Not Loading
- **Symptoms**: Double-clicking `.png` or `.bmp` files in the file browser doesn’t display them in the viewer.
- **Solutions**:
  - **Verify DLLs**: Ensure `libpng16.dll` and `zlib1.dll` are in the `Release`/`Debug` folder (e.g., `F:\Vs2017\Source\SDL_image\SDL2_image`).
    - Copy from `C:\vcpkg\installed\x64-windows\bin`.
  - **Check SDL2_image**: Confirm PNG support:
    - Run `vcpkg install sdl2-image[libwebp,avif,libjpeg-turbo]:x64-windows`.
    - Verify `IMG_Init(IMG_INIT_PNG)` in `Racoon.cpp` logs no errors.
  - **Test Files**: Ensure files are valid (open in Windows Photos).
  - **Logs**: Check `sdl_error.log` for `SDL2_image failed` or `stb_image failed`.
  - **Extension**: Ensure files have `.png` or `.bmp` extensions (e.g., rename `test` to `test.png`).

### 2. vcpkg Installation Errors
- **Error**: `sdl2-image has no feature named libpng`.
- **Solution**:
  - PNG support is included by default in `sdl2-image`. Use:
    ```cmd
    vcpkg install sdl2-image[libwebp,avif,libjpeg-turbo]:x64-windows sdl2-ttf:x64-windows libwebp:x64-windows libavif[dav1d]:x64-windows --recurse
     
    vcpkg integrate install
