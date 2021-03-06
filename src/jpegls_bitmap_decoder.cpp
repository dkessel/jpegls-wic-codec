﻿// Copyright (c) Team CharLS.
// SPDX-License-Identifier: MIT

#include "pch.h"

#include "jpegls_bitmap_decoder.h"

#include "class_factory.h"
#include "errors.h"
#include "guids.h"
#include "jpegls_bitmap_frame_decode.h"

#include <charls/charls.h>

#include <wincodec.h>
#include <winrt/base.h>

#include <array>
#include <mutex>

using charls::jpegls_category;
using charls::jpegls_decoder;
using charls::jpegls_error;
using std::array;
using std::error_code;
using std::mutex;
using std::scoped_lock;
using winrt::check_hresult;
using winrt::com_ptr;
using winrt::implements;
using winrt::make;
using winrt::to_hresult;


struct jpegls_bitmap_decoder final : implements<jpegls_bitmap_decoder, IWICBitmapDecoder>
{
    // IWICBitmapDecoder
    HRESULT __stdcall QueryCapability(_In_ IStream* stream, _Out_ DWORD* capability) noexcept override
    {
        TRACE("%p jpegls_bitmap_decoder::QueryCapability.1, stream=%p, capability=%p\n", this, stream, capability);

        if (!stream)
            return error_invalid_argument;

        if (!capability)
            return error_pointer;

        *capability = 0;

        // Custom decoder implementations should save the current position of the specified IStream,
        // read whatever information is necessary in order to determine which capabilities
        // it can provide for the supplied stream, and restore the stream position.
        try
        {
            array<std::byte, 4 * 1024> header{};
            unsigned long read_byte_count;

            check_hresult(stream->Read(header.data(), static_cast<ULONG>(header.size()), &read_byte_count));

            LARGE_INTEGER offset;
            offset.QuadPart = -static_cast<int64_t>(read_byte_count);
            check_hresult(stream->Seek(offset, STREAM_SEEK_CUR, nullptr));

            jpegls_decoder decoder;
            decoder.source(header.data(), read_byte_count);

            error_code error;
            decoder.read_header(error);
            if (error)
            {
                TRACE("%p jpegls_bitmap_decoder::QueryCapability.2, capability=0, ec=%d, (reason=%s)\n", this, error.value(),
                      jpegls_category().message(error.value()).c_str());
                return error_ok;
            }

            const auto& frame_info = decoder.frame_info();
            if (jpegls_bitmap_frame_decode::can_decode_to_wic_pixel_format(frame_info.bits_per_sample, frame_info.component_count))
            {
                *capability = WICBitmapDecoderCapabilityCanDecodeAllImages;
            }

            TRACE("%p jpegls_bitmap_decoder::QueryCapability.3, stream=%p, *capability=%d\n", this, stream, *capability);
            return error_ok;
        }
        catch (...)
        {
            return to_hresult();
        }
    }

    HRESULT __stdcall Initialize(_In_ IStream* stream, [[maybe_unused]] const WICDecodeOptions cache_options) noexcept override
    {
        TRACE("%p jpegls_bitmap_decoder::Initialize, stream=%p, cache_options=%d\n", this, stream, cache_options);

        if (!stream)
            return error_invalid_argument;

        WARNING_SUPPRESS_NEXT_LINE(26447) // noexcept: false warning, caused by scoped_lock
        scoped_lock lock{mutex_};

        stream_.copy_from(stream);
        bitmap_frame_decode_.attach(nullptr);

        return error_ok;
    }

    HRESULT __stdcall GetContainerFormat(_Out_ GUID* container_format) noexcept override
    {
        TRACE("%p jpegls_bitmap_decoder::GetContainerFormat, container_format=%p\n", this, container_format);

        if (!container_format)
            return error_pointer;

        *container_format = GUID_ContainerFormatJpegLS;
        return error_ok;
    }

    HRESULT __stdcall GetDecoderInfo(_Outptr_ IWICBitmapDecoderInfo** decoder_info) noexcept override
    {
        TRACE("%p jpegls_bitmap_decoder::GetContainerFormat, decoder_info=%p\n", this, decoder_info);

        try
        {
            com_ptr<IWICComponentInfo> component_info;
            check_hresult(imaging_factory()->CreateComponentInfo(CLSID_JpegLSDecoder, component_info.put()));
            check_hresult(component_info->QueryInterface(IID_PPV_ARGS(decoder_info)));

            return error_ok;
        }
        catch (...)
        {
            return to_hresult();
        }
    }

    HRESULT __stdcall CopyPalette([[maybe_unused]] _In_ IWICPalette* palette) noexcept override
    {
        TRACE("%p jpegls_bitmap_decoder::CopyPalette, palette=%p\n", this, palette);

        // Palettes are for JPEG-LS on frame level.
        return wincodec::error_palette_unavailable;
    }

    HRESULT __stdcall GetMetadataQueryReader([[maybe_unused]] _Outptr_ IWICMetadataQueryReader** metadata_query_reader) noexcept override
    {
        TRACE("%p jpegls_bitmap_decoder::GetMetadataQueryReader, metadata_query_reader=%p\n", this, metadata_query_reader);

        // Keep the initial design simple: no support for container-level metadata.
        const HRESULT result = wincodec::error_unsupported_operation;
        return result;
    }

    HRESULT __stdcall GetPreview([[maybe_unused]] _Outptr_ IWICBitmapSource** bitmap_source) noexcept override
    {
        TRACE("%p jpegls_bitmap_decoder::GetPreview, bitmap_source=%p\n", this, bitmap_source);
        const HRESULT result = wincodec::error_unsupported_operation;
        return result;
    }

    HRESULT __stdcall GetColorContexts([[maybe_unused]] const uint32_t count, [[maybe_unused]] IWICColorContext** color_contexts, [[maybe_unused]] uint32_t* actual_count) noexcept override
    {
        TRACE("%p jpegls_bitmap_decoder::GetColorContexts, count=%u, color_contexts=%p, actual_count=%p\n", this, count, color_contexts, actual_count);
        return wincodec::error_unsupported_operation;
    }

    HRESULT __stdcall GetThumbnail([[maybe_unused]] _Outptr_ IWICBitmapSource** thumbnail) noexcept override
    {
        TRACE("%p jpegls_bitmap_decoder::GetThumbnail, thumbnail=%p\n", this, thumbnail);
        const HRESULT result = wincodec::error_codec_no_thumbnail;
        return result;
    }

    HRESULT __stdcall GetFrameCount(_Out_ uint32_t* count) noexcept override
    {
        TRACE("%p jpegls_bitmap_decoder::GetFrameCount, count=%p\n", this, count);
        if (!count)
            return error_pointer;

        *count = 1; // JPEG-LS format can only store 1 frame.
        return error_ok;
    }

    HRESULT __stdcall GetFrame(const uint32_t index, _Outptr_ IWICBitmapFrameDecode** bitmap_frame_decode) noexcept override
    {
        TRACE("%p jpegls_bitmap_decoder::GetFrame, index=%d, bitmap_frame_decode=%p\n", this, index, bitmap_frame_decode);
        if (!bitmap_frame_decode)
        {
            const HRESULT result = error_pointer;
            return result;
        }

        if (index != 0)
        {
            const HRESULT result = wincodec::error_frame_missing;
            return result;
        }

        WARNING_SUPPRESS_NEXT_LINE(26447) // noexcept: false warning, caused by scoped_lock
        scoped_lock lock{mutex_};

        if (!stream_)
        {
            const HRESULT result = wincodec::error_not_initialized;
            return result;
        }

        try
        {
            if (!bitmap_frame_decode_)
            {
                bitmap_frame_decode_ = make<jpegls_bitmap_frame_decode>(stream_.get(), imaging_factory());
            }

            bitmap_frame_decode_.copy_to(bitmap_frame_decode);
            return error_ok;
        }
        catch (...)
        {
            return to_hresult();
        }
    }

private:
    IWICImagingFactory* imaging_factory()
    {
        if (!imaging_factory_)
        {
            check_hresult(CoCreateInstance(CLSID_WICImagingFactory,
                                           nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(imaging_factory_.put())));
        }

        return imaging_factory_.get();
    }

    mutex mutex_;
    com_ptr<IWICImagingFactory> imaging_factory_;
    com_ptr<IStream> stream_;
    com_ptr<IWICBitmapFrameDecode> bitmap_frame_decode_;
};

HRESULT create_jpegls_bitmap_decoder_factory(_In_ GUID const& interface_id, _Outptr_ void** result)
{
    return make<class_factory<jpegls_bitmap_decoder>>()->QueryInterface(interface_id, result);
}
