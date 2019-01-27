#pragma once

#include "../src/guids.h"

#include <Windows.h>
#include <wincodec.h>
#include <unknwn.h>
#include <winrt/base.h>

class factory
{
public:
    factory() noexcept(false)
    {
        library_ = LoadLibrary(L"jpegls-wic-codec.dll");
    }

    ~factory()
    {
        if (library_)
        {
            FreeLibrary(library_);
        }
    }

    factory(const factory&) = delete;
    factory(factory&&) = delete;
    factory& operator=(const factory&) = delete;
    factory& operator=(factory&&) = delete;

    winrt::com_ptr<IWICBitmapDecoder> CreateDecoder()
    {
        DllGetClassObjectPtr get_class_object = reinterpret_cast<DllGetClassObjectPtr>(GetProcAddress(library_, "DllGetClassObject"));
        if (!get_class_object)
            winrt::throw_last_error();

        winrt::com_ptr<IClassFactory> class_factory;
        winrt::check_hresult(get_class_object(CLSID_JpegLSDecoder, IID_PPV_ARGS(class_factory.put())));

        winrt::com_ptr<IWICBitmapDecoder> decoder;
        winrt::check_hresult(class_factory->CreateInstance(nullptr, IID_PPV_ARGS(decoder.put())));

        return decoder;
    }

    winrt::com_ptr<IWICBitmapEncoder> CreateEncoder()
    {
        DllGetClassObjectPtr get_class_object = reinterpret_cast<DllGetClassObjectPtr>(GetProcAddress(library_, "DllGetClassObject"));
        if (!get_class_object)
            winrt::throw_last_error();

        winrt::com_ptr<IClassFactory> class_factory;
        winrt::check_hresult(get_class_object(CLSID_JpegLSEncoder, IID_PPV_ARGS(class_factory.put())));

        winrt::com_ptr<IWICBitmapEncoder> encoder;
        winrt::check_hresult(class_factory->CreateInstance(nullptr, IID_PPV_ARGS(encoder.put())));

        return encoder;
    }

private:
    using DllGetClassObjectPtr = HRESULT(_stdcall *) (GUID const&, GUID const&, void** result);

    HINSTANCE library_{};
};
