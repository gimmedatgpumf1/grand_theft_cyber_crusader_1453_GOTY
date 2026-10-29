#!/usr/bin/env python3
"""Exercise real Vulkan swapchain code on a headless ICD, not a fake driver.

The test-only copies adapt surface creation to VK_EXT_headless_surface and force
swapchain recreation. The production build still uses the native window surface.
The headless surface has a fixed extent, so this does not simulate an OS resize.
"""
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
os.chdir(root)

class Extension(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char * 256), ("version", ctypes.c_uint32)]

try:
    loader = ctypes.CDLL("libvulkan.so.1")
    count = ctypes.c_uint32()
    enumerate_extensions = loader.vkEnumerateInstanceExtensionProperties
    if enumerate_extensions(None, ctypes.byref(count), None) != 0:
        raise OSError("extension enumeration failed")
    extensions = (Extension * count.value)()
    if enumerate_extensions(None, ctypes.byref(count), extensions) != 0:
        raise OSError("extension enumeration failed")
    if b"VK_EXT_headless_surface" not in {item.name for item in extensions}:
        raise OSError("VK_EXT_headless_surface unavailable")
except OSError as error:
    print(f"Vulkan headless swapchain: SKIP ({error})")
    raise SystemExit(0)

with tempfile.TemporaryDirectory(prefix="gtc-headless-wsi-") as temp:
    native = Path(temp) / "native"
    vk = native / "vulkan"
    vk.mkdir(parents=True)
    (native / "client").symlink_to(root / "native/client", target_is_directory=True)
    (native / "vendor").symlink_to(root / "native/vendor", target_is_directory=True)
    for name in ("vk_internal.h", "vk_renderer.h", "shaders"):
        (vk / name).symlink_to(root / "native/vulkan" / name)
    device = (root / "native/vulkan/vk_device.cpp").read_text()
    assert device.count("VK_KHR_xlib_surface") == 2
    (vk / "vk_device.cpp").write_text(device.replace("VK_KHR_xlib_surface", "VK_EXT_headless_surface"))
    swap = (root / "native/vulkan/vk_swapchain.cpp").read_text()
    start = swap.index("#else\n    const auto create = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(",
                       swap.index("bool Renderer::prepareSurface()"))
    end = swap.index("\n#endif", start)
    swap = swap[:start] + '''#else
    const auto create = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
        getInstanceProcAddr_(instance_, "vkCreateHeadlessSurfaceEXT"));
    if (!create) fatalSwapchain("headless surface entry point missing");
    VkHeadlessSurfaceCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
    created = create(instance_, &info, nullptr, &surface_);''' + swap[end:]
    fast_path = "if (swapReady_ && extent.width == swapExtent_.width && extent.height == swapExtent_.height) return true;"
    assert swap.count(fast_path) == 1
    swap = swap.replace(fast_path, "// Test only: recreate even at the same fixed headless extent.")
    (vk / "vk_swapchain.cpp").write_text(swap)
    source = ["tests/skeleton_swapchain_vulkan.cpp", "native/client/skeleton_room.cpp",
              "native/client/skeleton_body.cpp", "native/client/diagnostics.cpp", "native/client/raster.cpp",
              "native/client/hud.cpp", "native/client/ui_layer.cpp", str(vk / "vk_device.cpp"),
              "native/vulkan/vk_renderer.cpp", "native/vulkan/vk_upload.cpp", "native/vulkan/vk_batches.cpp",
              "native/vulkan/vk_targets.cpp", str(vk / "vk_swapchain.cpp")]
    output = Path(temp) / "test"
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-O2", "-pthread", "-Isrc", "-Inative",
                    *source, "-ldl", "-o", str(output)], check=True)
    subprocess.run([str(output)], check=True)
