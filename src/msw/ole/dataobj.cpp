///////////////////////////////////////////////////////////////////////////////
// Name:        msw/ole/dataobj.cpp
// Purpose:     implementation of wx[I]DataObject class
// Author:      Vadim Zeitlin
// Modified by:
// Created:     10.05.98
// RCS-ID:      $Id$
// Copyright:   (c) 1998 Vadim Zeitlin <zeitlin@dptmaths.ens-cachan.fr>
// Licence:     wxWindows license
///////////////////////////////////////////////////////////////////////////////

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

#ifdef __GNUG__
  #pragma implementation "dataobj.h"
#endif

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if defined(__BORLANDC__)
  #pragma hdrstop
#endif
#ifndef WX_PRECOMP
#include "wx/intl.h"
#endif
#include "wx/defs.h"

#if defined(__WIN32__) && !defined(__GNUWIN32__) || defined(wxUSE_NORLANDER_HEADERS)

#include "wx/log.h"
#include "wx/dataobj.h"

#include <windows.h>
#ifdef wxUSE_NORLANDER_HEADERS
  #include <ole2.h>
#endif
#include <oleauto.h>

#ifndef __WIN32__
  #include <ole2.h>
  #include <olestd.h>
#endif

#include  "wx/msw/ole/oleutils.h"

// ----------------------------------------------------------------------------
// functions
// ----------------------------------------------------------------------------

#ifdef __WXDEBUG__
    static const wxChar *GetTymedName(DWORD tymed);
#endif // Debug

// ----------------------------------------------------------------------------
// wxIEnumFORMATETC interface implementation
// ----------------------------------------------------------------------------

class wxIEnumFORMATETC : public IEnumFORMATETC
{
public:
    wxIEnumFORMATETC(const wxDataFormat* formats, ULONG nCount);
    ~wxIEnumFORMATETC() { delete [] m_formats; }

    DECLARE_IUNKNOWN_METHODS;

    // IEnumFORMATETC
    STDMETHODIMP Next(ULONG celt, FORMATETC *rgelt, ULONG *pceltFetched);
    STDMETHODIMP Skip(ULONG celt);
    STDMETHODIMP Reset();
    STDMETHODIMP Clone(IEnumFORMATETC **ppenum);

private:
    CLIPFORMAT *m_formats;  // formats we can provide data in
    ULONG       m_nCount,   // number of formats we support
                m_nCurrent; // current enum position
};

// ----------------------------------------------------------------------------
// wxIDataObject implementation of IDataObject interface
// ----------------------------------------------------------------------------

class wxIDataObject : public IDataObject
{
public:
    wxIDataObject(wxDataObject *pDataObject);
    ~wxIDataObject();

    // normally, wxDataObject controls our lifetime (i.e. we're deleted when it
    // is), but in some cases, the situation is inversed, that is we delete it
    // when this object is deleted - setting this flag enables such logic
    void SetDeleteFlag() { m_mustDelete = TRUE; }

    DECLARE_IUNKNOWN_METHODS;

    // IDataObject
    STDMETHODIMP GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium);
    STDMETHODIMP GetDataHere(FORMATETC *pformatetc, STGMEDIUM *pmedium);
    STDMETHODIMP QueryGetData(FORMATETC *pformatetc);
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC *In, FORMATETC *pOut);
    STDMETHODIMP SetData(FORMATETC *pfetc, STGMEDIUM *pmedium, BOOL fRelease);
    STDMETHODIMP EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFEtc);
    STDMETHODIMP DAdvise(FORMATETC *pfetc, DWORD ad, IAdviseSink *p, DWORD *pdw);
    STDMETHODIMP DUnadvise(DWORD dwConnection);
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA **ppenumAdvise);

private:
    wxDataObject *m_pDataObject;      // pointer to C++ class we belong to

    bool m_mustDelete;
};

// ----------------------------------------------------------------------------
// small helper class for getting screen DC (we're working with bitmaps and
// DIBs here)
// ----------------------------------------------------------------------------

class ScreenHDC
{
public:
    ScreenHDC() { m_hdc = GetDC(NULL);    }
   ~ScreenHDC() { ReleaseDC(NULL, m_hdc); }
    operator HDC() const { return m_hdc;  }

private:
    HDC m_hdc;
};

// ============================================================================
// implementation
// ============================================================================

// ----------------------------------------------------------------------------
// wxDataFormat
// ----------------------------------------------------------------------------

void wxDataFormat::SetId(const wxChar *format)
{
    m_format = ::RegisterClipboardFormat(format);
    if ( !m_format )
    {
        wxLogError(_("Couldn't register clipboard format '%s'."), format);
    }
}

wxString wxDataFormat::GetId() const
{
    static const int max = 256;

    wxString s;

    wxCHECK_MSG( !IsStandard(), s,
                 wxT("name of predefined format cannot be retrieved") );

    int len = ::GetClipboardFormatName(m_format, s.GetWriteBuf(max), max);
    s.UngetWriteBuf();

    if ( !len )
    {
        wxLogError(_("The clipboard format '%d' doesn't exist."), m_format);
    }

    return s;
}

// ----------------------------------------------------------------------------
// wxIEnumFORMATETC
// ----------------------------------------------------------------------------

BEGIN_IID_TABLE(wxIEnumFORMATETC)
    ADD_IID(Unknown)
    ADD_IID(EnumFORMATETC)
END_IID_TABLE;

IMPLEMENT_IUNKNOWN_METHODS(wxIEnumFORMATETC)

wxIEnumFORMATETC::wxIEnumFORMATETC(const wxDataFormat *formats, ULONG nCount)
{
    m_cRef = 0;
    m_nCurrent = 0;
    m_nCount = nCount;
    m_formats = new CLIPFORMAT[nCount];
    for ( ULONG n = 0; n < nCount; n++ ) {
        m_formats[n] = formats[n].GetFormatId();
    }
}

STDMETHODIMP wxIEnumFORMATETC::Next(ULONG      celt,
                                    FORMATETC *rgelt,
                                    ULONG     *pceltFetched)
{
    wxLogTrace(wxTRACE_OleCalls, wxT("wxIEnumFORMATETC::Next"));

    if ( celt > 1 ) {
        // we only return 1 element at a time - mainly because I'm too lazy to
        // implement something which you're never asked for anyhow
        return S_FALSE;
    }

    if ( m_nCurrent < m_nCount ) {
        FORMATETC format;
        format.cfFormat = m_formats[m_nCurrent++];
        format.ptd      = NULL;
        format.dwAspect = DVASPECT_CONTENT;
        format.lindex   = -1;
        format.tymed    = TYMED_HGLOBAL;
        *rgelt = format;

        return S_OK;
    }
    else {
        // bad index
        return S_FALSE;
    }
}

STDMETHODIMP wxIEnumFORMATETC::Skip(ULONG celt)
{
    wxLogTrace(wxTRACE_OleCalls, wxT("wxIEnumFORMATETC::Skip"));

    m_nCurrent += celt;
    if ( m_nCurrent < m_nCount )
        return S_OK;

    // no, can't skip this many elements
    m_nCurrent -= celt;

    return S_FALSE;
}

STDMETHODIMP wxIEnumFORMATETC::Reset()
{
    wxLogTrace(wxTRACE_OleCalls, wxT("wxIEnumFORMATETC::Reset"));

    m_nCurrent = 0;

    return S_OK;
}

STDMETHODIMP wxIEnumFORMATETC::Clone(IEnumFORMATETC **ppenum)
{
    wxLogTrace(wxTRACE_OleCalls, wxT("wxIEnumFORMATETC::Clone"));

    // unfortunately, we can't reuse the code in ctor - types are different
    wxIEnumFORMATETC *pNew = new wxIEnumFORMATETC(NULL, 0);
    pNew->m_nCount = m_nCount;
    pNew->m_formats = new CLIPFORMAT[m_nCount];
    for ( ULONG n = 0; n < m_nCount; n++ ) {
        pNew->m_formats[n] = m_formats[n];
    }
    pNew->AddRef();
    *ppenum = pNew;

    return S_OK;
}

// ----------------------------------------------------------------------------
// wxIDataObject
// ----------------------------------------------------------------------------

BEGIN_IID_TABLE(wxIDataObject)
    ADD_IID(Unknown)
    ADD_IID(DataObject)
END_IID_TABLE;

IMPLEMENT_IUNKNOWN_METHODS(wxIDataObject)

wxIDataObject::wxIDataObject(wxDataObject *pDataObject)
{
    m_cRef = 0;
    m_pDataObject = pDataObject;
    m_mustDelete = FALSE;
}

wxIDataObject::~wxIDataObject()
{
    if ( m_mustDelete )
    {
        delete m_pDataObject;
    }
}

// get data functions
STDMETHODIMP wxIDataObject::GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium)
{
    wxLogTrace(wxTRACE_OleCalls, wxT("wxIDataObject::GetData"));

    // is data is in our format?
    HRESULT hr = QueryGetData(pformatetcIn);
    if ( FAILED(hr) )
        return hr;

    // for the bitmaps and metafiles we use the handles instead of global memory
    // to pass the data
    wxDataFormat format = (wxDataFormatId)pformatetcIn->cfFormat;

    switch ( format )
    {
        case wxDF_BITMAP:
            pmedium->tymed = TYMED_GDI;
            break;

        case wxDF_METAFILE:
            pmedium->tymed = TYMED_MFPICT;
            break;

        default:
            // alloc memory
            size_t size = m_pDataObject->GetDataSize(format);
            if ( !size ) {
                // it probably means that the method is just not implemented
                wxLogDebug(wxT("Invalid data size - can't be 0"));

                return DV_E_FORMATETC;
            }

            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, size);
            if ( hGlobal == NULL ) {
                wxLogLastError("GlobalAlloc");
                return E_OUTOFMEMORY;
            }

            // copy data
            pmedium->tymed   = TYMED_HGLOBAL;
            pmedium->hGlobal = hGlobal;
    }

    pmedium->pUnkForRelease = NULL;

    // do copy the data
    hr = GetDataHere(pformatetcIn, pmedium);
    if ( FAILED(hr) ) {
        // free resources we allocated
        if ( pmedium->tymed == TYMED_HGLOBAL ) {
            GlobalFree(pmedium->hGlobal);
        }

        return hr;
    }

    return S_OK;
}

STDMETHODIMP wxIDataObject::GetDataHere(FORMATETC *pformatetc,
                                        STGMEDIUM *pmedium)
{
    wxLogTrace(wxTRACE_OleCalls, wxT("wxIDataObject::GetDataHere"));

    // put data in caller provided medium
    switch ( pmedium->tymed )
    {
        case TYMED_GDI:
            if ( !m_pDataObject->GetDataHere(wxDF_BITMAP, &pmedium->hBitmap) )
                return E_UNEXPECTED;
            break;

        case TYMED_MFPICT:
            // this should be copied on bitmaps - but I don't have time for
            // this now
            wxFAIL_MSG(wxT("TODO - no support for metafiles in wxDataObject"));
            break;

        case TYMED_HGLOBAL:
            {
                // copy data
                void *pBuf = GlobalLock(pmedium->hGlobal);
                if ( pBuf == NULL ) {
                    wxLogLastError(wxT("GlobalLock"));
                    return E_OUTOFMEMORY;
                }

                wxDataFormat format = (wxDataFormatId)pformatetc->cfFormat;
                if ( !m_pDataObject->GetDataHere(format, pBuf) )
                    return E_UNEXPECTED;

                GlobalUnlock(pmedium->hGlobal);
            }
            break;

        default:
            return DV_E_TYMED;
    }

    return S_OK;
}

// set data functions (not implemented)
STDMETHODIMP wxIDataObject::SetData(FORMATETC *pformatetc,
                                    STGMEDIUM *pmedium,
                                    BOOL       fRelease)
{
    wxLogTrace(wxTRACE_OleCalls, wxT("wxIDataObject::SetData"));

    switch ( pmedium->tymed )
    {
        case TYMED_GDI:
            m_pDataObject->SetData(wxDF_BITMAP, &pmedium->hBitmap);
            break;

        case TYMED_MFPICT:
            // this should be copied on bitmaps - but I don't have time for
            // this now
            wxFAIL_MSG(wxT("TODO - no support for metafiles in wxDataObject"));
            break;

        case TYMED_HGLOBAL:
            {
                // copy data
                void *pBuf = GlobalLock(pmedium->hGlobal);
                if ( pBuf == NULL ) {
                    wxLogLastError("GlobalLock");

                    return E_OUTOFMEMORY;
                }

                wxDataFormat format = (wxDataFormatId)pformatetc->cfFormat;
                m_pDataObject->SetData(format, pBuf);

                GlobalUnlock(pmedium->hGlobal);
            }
            break;

        default:
            return DV_E_TYMED;
    }

    if ( fRelease ) {
        // we own the medium, so we must release it - but do *not* free the
        // bitmap handle fi we have it because we have copied it elsewhere
        if ( pmedium->tymed == TYMED_GDI )
        {
            pmedium->hBitmap = 0;
        }

        ReleaseStgMedium(pmedium);
    }

    return S_OK;
}

// information functions
STDMETHODIMP wxIDataObject::QueryGetData(FORMATETC *pformatetc)
{
    // do we accept data in this format?
    if ( pformatetc == NULL ) {
        wxLogTrace(wxTRACE_OleCalls,
                   wxT("wxIDataObject::QueryGetData: invalid ptr."));

        return E_INVALIDARG;
    }

    // the only one allowed by current COM implementation
    if ( pformatetc->lindex != -1 ) {
        wxLogTrace(wxTRACE_OleCalls,
                   wxT("wxIDataObject::QueryGetData: bad lindex %d"),
                   pformatetc->lindex);

        return DV_E_LINDEX;
    }

    // we don't support anything other (THUMBNAIL, ICON, DOCPRINT...)
    if ( pformatetc->dwAspect != DVASPECT_CONTENT ) {
        wxLogTrace(wxTRACE_OleCalls,
                   wxT("wxIDataObject::QueryGetData: bad dwAspect %d"),
                   pformatetc->dwAspect);

        return DV_E_DVASPECT;
    }

    // and now check the type of data requested
    wxDataFormat format = (wxDataFormatId)pformatetc->cfFormat;
    if ( m_pDataObject->IsSupportedFormat(format) ) {
#ifdef __WXDEBUG__
        wxLogTrace(wxTRACE_OleCalls, wxT("wxIDataObject::QueryGetData: %s ok"),
                   wxDataObject::GetFormatName(format));
#endif // Debug
    }
    else {
#ifdef __WXDEBUG__
        wxLogTrace(wxTRACE_OleCalls,
                   wxT("wxIDataObject::QueryGetData: %s unsupported"),
                   wxDataObject::GetFormatName(format));
#endif
        return DV_E_FORMATETC;
    }

    // we only transfer data by global memory, except for some particular cases
    DWORD tymed = pformatetc->tymed;
    if ( (format == wxDF_BITMAP && !(tymed & TYMED_GDI)) &&
         !(tymed & TYMED_HGLOBAL) ) {
        // it's not what we're waiting for
#ifdef __WXDEBUG__
        wxLogTrace(wxTRACE_OleCalls,
                   wxT("wxIDataObject::QueryGetData: %s != %s"),
                   GetTymedName(tymed),
                   GetTymedName(format == wxDF_BITMAP ? TYMED_GDI
                                                      : TYMED_HGLOBAL));
#endif // Debug

        return DV_E_TYMED;
    }

    return S_OK;
}

STDMETHODIMP wxIDataObject::GetCanonicalFormatEtc(FORMATETC *pFormatetcIn,
                                                  FORMATETC *pFormatetcOut)
{
    wxLogTrace(wxTRACE_OleCalls, wxT("wxIDataObject::GetCanonicalFormatEtc"));

    // TODO we might want something better than this trivial implementation here
    if ( pFormatetcOut != NULL )
        pFormatetcOut->ptd = NULL;

    return DATA_S_SAMEFORMATETC;
}

STDMETHODIMP wxIDataObject::EnumFormatEtc(DWORD dwDirection,
                                          IEnumFORMATETC **ppenumFormatEtc)
{
    wxLogTrace(wxTRACE_OleCalls, wxT("wxIDataObject::EnumFormatEtc"));

    bool allowOutputOnly = dwDirection == DATADIR_GET;

    size_t nFormatCount = m_pDataObject->GetFormatCount(allowOutputOnly);
    wxDataFormat format, *formats;
    if ( nFormatCount == 1 ) {
        // this is the most common case, this is why we consider it separately
        formats = &format;
        format = m_pDataObject->GetPreferredFormat();
    }
    else {
        // bad luck, build the array with all formats
        formats = new wxDataFormat[nFormatCount];
        m_pDataObject->GetAllFormats(formats, allowOutputOnly);
    }

    wxIEnumFORMATETC *pEnum = new wxIEnumFORMATETC(formats, nFormatCount);
    pEnum->AddRef();
    *ppenumFormatEtc = pEnum;

    if ( formats != &format ) {
        delete [] formats;
    }

    return S_OK;
}

// advise sink functions (not implemented)
STDMETHODIMP wxIDataObject::DAdvise(FORMATETC   *pformatetc,
                                    DWORD        advf,
                                    IAdviseSink *pAdvSink,
                                    DWORD       *pdwConnection)
{
  return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP wxIDataObject::DUnadvise(DWORD dwConnection)
{
  return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP wxIDataObject::EnumDAdvise(IEnumSTATDATA **ppenumAdvise)
{
  return OLE_E_ADVISENOTSUPPORTED;
}

// ----------------------------------------------------------------------------
// wxDataObject
// ----------------------------------------------------------------------------

wxDataObject::wxDataObject()
{
    m_pIDataObject = new wxIDataObject(this);
    m_pIDataObject->AddRef();
}

wxDataObject::~wxDataObject()
{
    ReleaseInterface(m_pIDataObject);
}

void wxDataObject::SetAutoDelete()
{
    ((wxIDataObject *)m_pIDataObject)->SetDeleteFlag();
    m_pIDataObject->Release();

    // so that the dtor doesnt' crash
    m_pIDataObject = NULL;
}

bool wxDataObject::IsSupportedFormat(const wxDataFormat& format) const
{
    size_t nFormatCount = GetFormatCount();
    if ( nFormatCount == 1 ) {
        return format == GetPreferredFormat();
    }
    else {
        wxDataFormat *formats = new wxDataFormat[nFormatCount];
        GetAllFormats(formats);

        size_t n;
        for ( n = 0; n < nFormatCount; n++ ) {
            if ( formats[n] == format )
                break;
        }

        delete [] formats;

        // found?
        return n < nFormatCount;
    }
}

#ifdef __WXDEBUG__
const char *wxDataObject::GetFormatName(wxDataFormat format)
{
  // case 'xxx' is not a valid value for switch of enum 'wxDataFormat'
  #ifdef __VISUALC__
    #pragma warning(disable:4063)
  #endif // VC++

  static char s_szBuf[128];
  switch ( format ) {
    case CF_TEXT:         return "CF_TEXT";
    case CF_BITMAP:       return "CF_BITMAP";
    case CF_METAFILEPICT: return "CF_METAFILEPICT";
    case CF_SYLK:         return "CF_SYLK";
    case CF_DIF:          return "CF_DIF";
    case CF_TIFF:         return "CF_TIFF";
    case CF_OEMTEXT:      return "CF_OEMTEXT";
    case CF_DIB:          return "CF_DIB";
    case CF_PALETTE:      return "CF_PALETTE";
    case CF_PENDATA:      return "CF_PENDATA";
    case CF_RIFF:         return "CF_RIFF";
    case CF_WAVE:         return "CF_WAVE";
    case CF_UNICODETEXT:  return "CF_UNICODETEXT";
    case CF_ENHMETAFILE:  return "CF_ENHMETAFILE";
    case CF_HDROP:        return "CF_HDROP";
    case CF_LOCALE:       return "CF_LOCALE";
    default:
      sprintf(s_szBuf, "clipboard format 0x%x (unknown)", format);
      return s_szBuf;
  }

  #ifdef __VISUALC__
    #pragma warning(default:4063)
  #endif // VC++
}
#endif // Debug

// ----------------------------------------------------------------------------
// wxPrivateDataObject
// ----------------------------------------------------------------------------

wxPrivateDataObject::wxPrivateDataObject()
{
    m_size = 0;
    m_data = NULL;
}

void wxPrivateDataObject::Free()
{
    if ( m_data )
        free(m_data);
}

void wxPrivateDataObject::SetData( const void *data, size_t size )
{
    Free();

    m_size = size;
    m_data = malloc(size);

    memcpy( m_data, data, size );
}

void wxPrivateDataObject::WriteData( void *dest ) const
{
    WriteData( m_data, dest );
}

size_t wxPrivateDataObject::GetSize() const
{
    return m_size;
}

void wxPrivateDataObject::WriteData( const void *data, void *dest ) const
{
    memcpy( dest, data, GetSize() );
}

// ----------------------------------------------------------------------------
// wxBitmapDataObject: it supports standard CF_BITMAP and CF_DIB formats
// ----------------------------------------------------------------------------

size_t wxBitmapDataObject::GetFormatCount(bool outputOnlyToo) const
{
    return 2;
}

void wxBitmapDataObject::GetAllFormats(wxDataFormat *formats,
                                       bool outputOnlyToo) const
{
    formats[0] = CF_BITMAP;
    formats[1] = CF_DIB;
}

// the bitmaps aren't passed by value as other types of data (i.e. by copyign
// the data into a global memory chunk and passing it to the clipboard or
// another application or whatever), but by handle, so these generic functions
// don't make much sense to them.

size_t wxBitmapDataObject::GetDataSize(const wxDataFormat& format) const
{
    if ( format.GetFormatId() == CF_DIB )
    {
        // create the DIB
        ScreenHDC hdc;

        // shouldn't be selected into a DC or GetDIBits() would fail
        wxASSERT_MSG( !m_bitmap.GetSelectedInto(),
                      wxT("can't copy bitmap selected into wxMemoryDC") );

        // first get the info
        BITMAPINFO bi;
        if ( !GetDIBits(hdc, (HBITMAP)m_bitmap.GetHBITMAP(), 0, 0,
                        NULL, &bi, DIB_RGB_COLORS) )
        {
            wxLogLastError("GetDIBits(NULL)");

            return 0;
        }

        return sizeof(BITMAPINFO) + bi.bmiHeader.biSizeImage;
    }
    else // CF_BITMAP
    {
        // no data to copy - we don't pass HBITMAP via global memory
        return 0;
    }
}

bool wxBitmapDataObject::GetDataHere(const wxDataFormat& format,
                                     void *pBuf) const
{
    wxASSERT_MSG( m_bitmap.Ok(), wxT("copying invalid bitmap") );

    HBITMAP hbmp = (HBITMAP)m_bitmap.GetHBITMAP();
    if ( format.GetFormatId() == CF_DIB )
    {
        // create the DIB
        ScreenHDC hdc;

        // shouldn't be selected into a DC or GetDIBits() would fail
        wxASSERT_MSG( !m_bitmap.GetSelectedInto(),
                      wxT("can't copy bitmap selected into wxMemoryDC") );

        // first get the info
        BITMAPINFO *pbi = (BITMAPINFO *)pBuf;
        if ( !GetDIBits(hdc, hbmp, 0, 0, NULL, pbi, DIB_RGB_COLORS) )
        {
            wxLogLastError("GetDIBits(NULL)");

            return 0;
        }

        // and now copy the bits
        if ( !GetDIBits(hdc, hbmp, 0, pbi->bmiHeader.biHeight, pbi + 1,
                        pbi, DIB_RGB_COLORS) )
        {
            wxLogLastError("GetDIBits");

            return FALSE;
        }
    }
    else // CF_BITMAP
    {
        // we put a bitmap handle into pBuf
        *(HBITMAP *)pBuf = hbmp;
    }

    return TRUE;
}

bool wxBitmapDataObject::SetData(const wxDataFormat& format, const void *pBuf)
{
    HBITMAP hbmp;
    if ( format.GetFormatId() == CF_DIB )
    {
        // here we get BITMAPINFO struct followed by the actual bitmap bits and
        // BITMAPINFO starts with BITMAPINFOHEADER followed by colour info
        ScreenHDC hdc;

        BITMAPINFO *pbmi = (BITMAPINFO *)pBuf;
        BITMAPINFOHEADER *pbmih = &pbmi->bmiHeader;
        hbmp = CreateDIBitmap(hdc, pbmih, CBM_INIT,
                              pbmi + 1, pbmi, DIB_RGB_COLORS);
        if ( !hbmp )
        {
            wxLogLastError("CreateDIBitmap");
        }

        m_bitmap.SetWidth(pbmih->biWidth);
        m_bitmap.SetHeight(pbmih->biHeight);
    }
    else // CF_BITMAP
    {
        // it's easy with bitmaps: we pass them by handle
        hbmp = *(HBITMAP *)pBuf;

        BITMAP bmp;
        if ( !GetObject(hbmp, sizeof(BITMAP), &bmp) )
        {
            wxLogLastError("GetObject(HBITMAP)");
        }

        m_bitmap.SetWidth(bmp.bmWidth);
        m_bitmap.SetHeight(bmp.bmHeight);
        m_bitmap.SetDepth(bmp.bmPlanes);
    }

    m_bitmap.SetHBITMAP((WXHBITMAP)hbmp);

    wxASSERT_MSG( m_bitmap.Ok(), wxT("pasting invalid bitmap") );

    return TRUE;
}

// ----------------------------------------------------------------------------
// private functions
// ----------------------------------------------------------------------------

#ifdef __WXDEBUG__

static const wxChar *GetTymedName(DWORD tymed)
{
    static wxChar s_szBuf[128];
    switch ( tymed ) {
        case TYMED_HGLOBAL:   return wxT("TYMED_HGLOBAL");
        case TYMED_FILE:      return wxT("TYMED_FILE");
        case TYMED_ISTREAM:   return wxT("TYMED_ISTREAM");
        case TYMED_ISTORAGE:  return wxT("TYMED_ISTORAGE");
        case TYMED_GDI:       return wxT("TYMED_GDI");
        case TYMED_MFPICT:    return wxT("TYMED_MFPICT");
        case TYMED_ENHMF:     return wxT("TYMED_ENHMF");
        default:
            wxSprintf(s_szBuf, wxT("type of media format %d (unknown)"), tymed);
            return s_szBuf;
    }
}

#endif // Debug

#endif // not using OLE at all

