#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include "ocr.h"
#include <cstring>
const char* native_ocr_test() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        auto engine=winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(winrt::Windows::Globalization::Language(L"en-US"));
        if(!engine) return "unavailable_language_or_package_identity";
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=400; info.bmiHeader.biHeight=-80; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        void* pixels=nullptr; HDC dc=CreateCompatibleDC(nullptr); HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(!dc||!bitmap||!pixels) { if(bitmap) DeleteObject(bitmap); if(dc) DeleteDC(dc); return "image_allocation_failed"; }
        HGDIOBJ oldBitmap=SelectObject(dc,bitmap); std::memset(pixels,255,400*80*4);
        HFONT font=CreateFontW(40,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");
        HGDIOBJ oldFont=SelectObject(dc,font); SetTextColor(dc,RGB(0,0,0)); SetBkColor(dc,RGB(255,255,255)); TextOutW(dc,10,10,L"Cliptiq",7);
        GdiFlush();
        winrt::Windows::Storage::Streams::DataWriter writer;
        writer.WriteBytes(winrt::array_view<const uint8_t>((const uint8_t*)pixels,(const uint8_t*)pixels+400*80*4));
        SelectObject(dc,oldFont); SelectObject(dc,oldBitmap); DeleteObject(font); DeleteObject(bitmap); DeleteDC(dc);
        winrt::Windows::Graphics::Imaging::SoftwareBitmap image(winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,400,80,winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Ignore);
        image.CopyFromBuffer(writer.DetachBuffer());
        auto result=engine.RecognizeAsync(image).get();
        auto text=winrt::to_string(result.Text());
        return text.find("Cliptiq")!=std::string::npos ? "recognized" : "completed_text_mismatch";
    } catch(const winrt::hresult_error&) { return "unavailable_hresult_identity_or_engine"; }
}
