/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// This is a GPU-backend specific test. It relies on static intializers to work

#include "SkTypes.h"

#if SK_SUPPORT_GPU && defined(SK_BUILD_FOR_ANDROID) && __ANDROID_API__ >= 26

#include "GrAHardwareBufferImageGenerator.h"
#include "GrContext.h"
#include "GrContextFactory.h"
#include "GrContextPriv.h"
#include "GrGpu.h"
#include "SkImage.h"
#include "SkSurface.h"
#include "Test.h"

#include <android/hardware_buffer.h>
#include <cinttypes>

static const int DEV_W = 16, DEV_H = 16;

static SkPMColor get_src_color(int x, int y) {
    SkASSERT(x >= 0 && x < DEV_W);
    SkASSERT(y >= 0 && y < DEV_H);

    U8CPU r = x;
    U8CPU g = y;
    U8CPU b = 0xc;

    U8CPU a = 0xff;
    switch ((x+y) % 5) {
        case 0:
            a = 0xff;
            break;
        case 1:
            a = 0x80;
            break;
        case 2:
            a = 0xCC;
            break;
        case 4:
            a = 0x01;
            break;
        case 3:
            a = 0x00;
            break;
    }
    a = 0xff;
    return SkPremultiplyARGBInline(a, r, g, b);
}

static SkBitmap make_src_bitmap() {
    static SkBitmap bmp;
    if (bmp.isNull()) {
        bmp.allocN32Pixels(DEV_W, DEV_H);
        intptr_t pixels = reinterpret_cast<intptr_t>(bmp.getPixels());
        for (int y = 0; y < DEV_H; ++y) {
            for (int x = 0; x < DEV_W; ++x) {
                SkPMColor* pixel = reinterpret_cast<SkPMColor*>(
                        pixels + y * bmp.rowBytes() + x * bmp.bytesPerPixel());
                *pixel = get_src_color(x, y);
            }
        }
    }
    return bmp;
}

static bool check_read(skiatest::Reporter* reporter, const SkBitmap& expectedBitmap,
                       const SkBitmap& actualBitmap) {
    bool result = true;
    for (int y = 0; y < DEV_H && result; ++y) {
        for (int x = 0; x < DEV_W && result; ++x) {
            const uint32_t srcPixel = *expectedBitmap.getAddr32(x, y);
            const uint32_t dstPixel = *actualBitmap.getAddr32(x, y);
            if (srcPixel != dstPixel) {
                ERRORF(reporter, "Expected readback pixel (%d, %d) value 0x%08x, got 0x%08x.",
                       x, y,  srcPixel, dstPixel);
                result = false;
            }
        }
    }
    return result;
}

static void cleanup_resources(AHardwareBuffer* buffer) {
    if (buffer) {
        AHardwareBuffer_release(buffer);
    }
}

// Basic test to make sure we can import an AHardwareBuffer into an SkImage and draw it into a
// surface.
DEF_GPUTEST_FOR_RENDERING_CONTEXTS(GrAHardwareBuffer_BasicDrawTest,
                                   reporter, context_info) {
    GrContext* context = context_info.grContext();
    if (!context->contextPriv().caps()->supportsAHardwareBufferImages()) {
        return;
    }

    ///////////////////////////////////////////////////////////////////////////
    // Setup SkBitmaps
    ///////////////////////////////////////////////////////////////////////////

    const SkBitmap srcBitmap = make_src_bitmap();

    ///////////////////////////////////////////////////////////////////////////
    // Setup AHardwareBuffer
    ///////////////////////////////////////////////////////////////////////////

    AHardwareBuffer* buffer = nullptr;

    AHardwareBuffer_Desc hwbDesc;
    hwbDesc.width = DEV_W;
    hwbDesc.height = DEV_H;
    hwbDesc.layers = 1;
    hwbDesc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_NEVER |
                    AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                    AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    hwbDesc.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    // The following three are not used in the allocate
    hwbDesc.stride = 0;
    hwbDesc.rfu0= 0;
    hwbDesc.rfu1= 0;

    if (int error = AHardwareBuffer_allocate(&hwbDesc, &buffer)) {
        ERRORF(reporter, "Failed to allocated hardware buffer, error: %d", error);
        cleanup_resources(buffer);
        return;
    }

    // Get actual desc for allocated buffer so we know the stride for uploading cpu data.
    AHardwareBuffer_describe(buffer, &hwbDesc);

    uint32_t* bufferAddr;
    if (AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr,
                             reinterpret_cast<void**>(&bufferAddr))) {
        ERRORF(reporter, "Failed to lock hardware buffer");
        cleanup_resources(buffer);
        return;
    }

    int bbp = srcBitmap.bytesPerPixel();
    uint32_t* src = (uint32_t*)srcBitmap.getPixels();
    uint32_t* dst = bufferAddr;
    for (int y = 0; y < DEV_H; ++y) {
        memcpy(dst, src, DEV_W * bbp);
        src += DEV_W;
        dst += hwbDesc.stride;
    }
    AHardwareBuffer_unlock(buffer, nullptr);

    ///////////////////////////////////////////////////////////////////////////
    // Wrap AHardwareBuffer in SkImage
    ///////////////////////////////////////////////////////////////////////////

    sk_sp<SkImage> image = SkImage::MakeFromAHardwareBuffer(buffer);
    REPORTER_ASSERT(reporter, image);

    ///////////////////////////////////////////////////////////////////////////
    // Make a surface to draw into
    ///////////////////////////////////////////////////////////////////////////

    SkImageInfo imageInfo = SkImageInfo::Make(DEV_W, DEV_H, kRGBA_8888_SkColorType,
                                              kPremul_SkAlphaType);
    sk_sp<SkSurface> surface = SkSurface::MakeRenderTarget(context, SkBudgeted::kNo,
                                                           imageInfo);
    REPORTER_ASSERT(reporter, surface);

    ///////////////////////////////////////////////////////////////////////////
    // Draw the AHardwareBuffer SkImage into surface
    ///////////////////////////////////////////////////////////////////////////

    surface->getCanvas()->drawImage(image, 0, 0);

    SkBitmap readbackBitmap;
    readbackBitmap.allocN32Pixels(DEV_W, DEV_H);

    REPORTER_ASSERT(reporter, surface->readPixels(readbackBitmap, 0, 0));
    REPORTER_ASSERT(reporter, check_read(reporter, srcBitmap, readbackBitmap));

    image.reset();

    cleanup_resources(buffer);
}

#endif