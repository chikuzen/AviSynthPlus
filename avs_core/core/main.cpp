// Avisynth v2.5.  Copyright 2007 Ben Rudiak-Gould et al.
// http://www.avisynth.org

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA, or visit
// http://www.gnu.org/copyleft/gpl.html .
//
// Linking Avisynth statically or dynamically with other modules is making a
// combined work based on Avisynth.  Thus, the terms and conditions of the GNU
// General Public License cover the whole combination.
//
// As a special exception, the copyright holders of Avisynth give you
// permission to link Avisynth with independent modules that communicate with
// Avisynth solely through the interfaces defined in avisynth.h, regardless of the license
// terms of these independent modules, and to copy and distribute the
// resulting combined work under terms of your choice, provided that
// every copy of the combined work is accompanied by a complete copy of
// the source code of Avisynth (the version of Avisynth used to produce the
// combined work), being distributed under the terms of the GNU General
// Public License plus this exception.  An independent module is a module
// which is not derived from or based on Avisynth, such as 3rd-party filters,
// import and export plugins, or graphical user interfaces.


#include <avisynth.h>
#include "internal.h"
#include <avs/win.h>
#include <avs/minmax.h>
#include "bitblt.h"
#include "exception.h"
#include <vfw.h>
#include <cstdio>
#include <new>
#include <intrin.h>

#define FP_STATE 0x9001f


#include <float.h>


#ifndef _DEBUG
// Release mode logging
// #define OPT_RELS_LOGGING
# ifdef OPT_RELS_LOGGING

#undef _RPT0
#undef _RPT1
#undef _RPT2
#undef _RPT3
#undef _RPT4
#  ifdef _RPT5
#undef _RPT5
#  endif

#define _RPT0(rptno, msg)                     ReportMe(msg)                         
#define _RPT1(rptno, msg, a1)                 ReportMe(msg, a1)                  
#define _RPT2(rptno, msg, a1, a2)             ReportMe(msg, a1, a2)            
#define _RPT3(rptno, msg, a1, a2, a3)         ReportMe(msg, a1, a2, a3)      
#define _RPT4(rptno, msg, a1, a2, a3, a4)     ReportMe(msg, a1, a2, a3, a4)
#define _RPT5(rptno, msg, a1, a2, a3, a4, a5) ReportMe(msg, a1, a2, a3, a4, a5)

void ReportMe(const char * msg, ...) {
  static char buf[256] = "";
  va_list args;
  int l = strlen(buf);

  va_start(args, msg);
  l = _vsnprintf(buf+l, sizeof(buf)-1-l, msg, args);
  buf[sizeof(buf)-1] = 0;
  va_end(args);

  if (l == -1 || strchr(buf, '\n')) {
    OutputDebugString(buf);
    buf[0] = 0;
  }
}

# else
#  ifndef _RPT5
#define _RPT5(rptno, msg, a1, a2, a3, a4, a5)
#  endif
# endif
#else
# ifndef _RPT5
#  ifdef _RPT_BASE
#define _RPT5(rptno, msg, a1, a2, a3, a4, a5) \
  _RPT_BASE((rptno, NULL, 0, NULL, msg, a1, a2, a3, a4, a5))
#  else
#   ifdef _CrtDbgBreak
#define _RPT5(rptno, msg, a1, a2, a3, a4, a5) \
  do { if ((1 == _CrtDbgReport(rptno, NULL, 0, NULL, msg, a1, a2, a3, a4, a5))) \
  _CrtDbgBreak(); } while (0)
#   else
#define _RPT5(rptno, msg, a1, a2, a3, a4, a5)
#   endif
#  endif
# endif
#endif

static long gRefCnt=0;


extern "C" const GUID CLSID_CAVIFileSynth   // {E6D6B700-124D-11D4-86F3-DB80AFD98778}
  = {0xe6d6b700, 0x124d, 0x11d4, {0x86, 0xf3, 0xdb, 0x80, 0xaf, 0xd9, 0x87, 0x78}};

extern "C" const GUID IID_IAvisynthClipInfo   // {E6D6B708-124D-11D4-86F3-DB80AFD98778}
  = {0xe6d6b708, 0x124d, 0x11d4, {0x86, 0xf3, 0xdb, 0x80, 0xaf, 0xd9, 0x87, 0x78}};


struct IAvisynthClipInfo : IUnknown {
  virtual int __stdcall GetError(const char** ppszMessage) = 0;
  virtual bool __stdcall GetParity(int n) = 0;
  virtual bool __stdcall IsFieldBased() = 0;
};


class CAVIFileSynth: public IAVIFile, public IPersistFile, public IClassFactory, public IAvisynthClipInfo {
  friend class CAVIStreamSynth;
private:
  long m_refs;

  char* szScriptName;
  IScriptEnvironment2* env;
  PClip filter_graph;
  const VideoInfo* vi;
  const char* error_msg;

  CRITICAL_SECTION cs_filter_graph;

  bool VDubPlanarHack;
  bool AVIPadScanlines;

  int ImageSize();

  bool DelayInit();
  bool DelayInit2();

  void MakeErrorStream(const char* msg);

  void Lock();
  void Unlock();

public:

  CAVIFileSynth(const CLSID& rclsid);
  ~CAVIFileSynth();

  static HRESULT Create(const CLSID& rclsid, const IID& riid, void **ppv);

  //////////// IUnknown

  STDMETHODIMP QueryInterface(const IID& iid, void **ppv);
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();

  //////////// IClassFactory

  STDMETHODIMP CreateInstance (LPUNKNOWN pUnkOuter, REFIID riid,  void * * ppvObj) ;
  STDMETHODIMP LockServer (BOOL fLock) ;

  //////////// IPersistFile

  STDMETHODIMP GetClassID(LPCLSID lpClassID);  // IPersist

  STDMETHODIMP IsDirty();
  STDMETHODIMP Load(LPCOLESTR lpszFileName, DWORD grfMode);
  STDMETHODIMP Save(LPCOLESTR lpszFileName, BOOL fRemember);
  STDMETHODIMP SaveCompleted(LPCOLESTR lpszFileName);
  STDMETHODIMP GetCurFile(LPOLESTR *lplpszFileName);

  //////////// IAVIFile

  STDMETHODIMP CreateStream(PAVISTREAM *ppStream, AVISTREAMINFOW *psi);       // 5
  STDMETHODIMP EndRecord();                                                   // 8
  STDMETHODIMP GetStream(PAVISTREAM *ppStream, DWORD fccType, LONG lParam);   // 4
  STDMETHODIMP Info(AVIFILEINFOW *psi, LONG lSize);                           // 3

  STDMETHODIMP Open(LPCSTR szFile, UINT mode, LPCOLESTR lpszFileName);        // ???
  STDMETHODIMP Save(LPCSTR szFile, AVICOMPRESSOPTIONS FAR *lpOptions,         // ???
    AVISAVECALLBACK lpfnCallback);

  STDMETHODIMP ReadData(DWORD fcc, LPVOID lp, LONG *lpcb);                    // 7
  STDMETHODIMP WriteData(DWORD fcc, LPVOID lpBuffer, LONG cbBuffer);          // 6
  STDMETHODIMP DeleteStream(DWORD fccType, LONG lParam);                      // 9

  //////////// IAvisynthClipInfo

  int __stdcall GetError(const char** ppszMessage);
  bool __stdcall GetParity(int n);
  bool __stdcall IsFieldBased();
};

///////////////////////////////////

class CAVIStreamSynth;

class CAVIStreamSynth: public IAVIStream, public IAVIStreaming {
public:

  //////////// IUnknown

  STDMETHODIMP QueryInterface(const IID& iid, void **ppv);
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();

  CAVIStreamSynth(CAVIFileSynth *parentPtr, bool isAudio);
  ~CAVIStreamSynth();

  //////////// IAVIStream

  STDMETHODIMP Create(LPARAM lParam1, LPARAM lParam2);
  STDMETHODIMP Delete(LONG lStart, LONG lSamples);
  STDMETHODIMP_(LONG) Info(AVISTREAMINFOW *psi, LONG lSize);
  STDMETHODIMP_(LONG) FindSample(LONG lPos, LONG lFlags);
  STDMETHODIMP Read(LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, LONG *plBytes, LONG *plSamples);
  STDMETHODIMP ReadData(DWORD fcc, LPVOID lp, LONG *lpcb);
  STDMETHODIMP ReadFormat(LONG lPos, LPVOID lpFormat, LONG *lpcbFormat);
  STDMETHODIMP SetFormat(LONG lPos, LPVOID lpFormat, LONG cbFormat);
  STDMETHODIMP Write(LONG lStart, LONG lSamples, LPVOID lpBuffer,
    LONG cbBuffer, DWORD dwFlags, LONG FAR *plSampWritten, 
    LONG FAR *plBytesWritten);
  STDMETHODIMP WriteData(DWORD fcc, LPVOID lpBuffer, LONG cbBuffer);
  STDMETHODIMP SetInfo(AVISTREAMINFOW *psi, LONG lSize);

  //////////// IAVIStreaming

  STDMETHODIMP Begin(LONG lStart, LONG lEnd, LONG lRate);
  STDMETHODIMP End();

private:
  long m_refs;

  CAVIFileSynth *parent;
  BOOL fAudio;

  const char *sName;

  //////////// internal

  void ReadFrame(void* lpBuffer, int n);

  HRESULT Read2(LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, LONG *plBytes, LONG *plSamples);
};


BOOL APIENTRY DllMain(HANDLE hModule, ULONG ulReason, LPVOID lpReserved) {

  _RPT4(0,"DllMain: hModule=0x%08x, ulReason=%x, lpReserved=0x%08x, gRefCnt = %ld\n",
    hModule, ulReason, lpReserved, gRefCnt);

  return TRUE;
}

STDAPI DllGetClassObject(IN REFCLSID rclsid, IN REFIID riid, OUT LPVOID FAR* ppv){

  if (rclsid != CLSID_CAVIFileSynth) {
    _RPT0(0,"DllGetClassObject() CLASS_E_CLASSNOTAVAILABLE\n");
    return CLASS_E_CLASSNOTAVAILABLE;
  }
  _RPT0(0,"DllGetClassObject() CLSID: CAVIFileSynth\n");

  HRESULT hresult = CAVIFileSynth::Create(rclsid, riid, ppv);

  _RPT2(0,"DllGetClassObject() result=0x%X, object=%p\n", hresult, *ppv);

  return hresult;
}

STDAPI DllCanUnloadNow() {
  _RPT1(0,"DllCanUnloadNow(): gRefCnt = %ld\n", gRefCnt);

  return gRefCnt ? S_FALSE : S_OK;
}


///////////////////////////////////////////////////////////////////////////
//
//	CAVIFileSynth
//
///////////////////////////////////////////////////////////////////////////
//////////// IClassFactory

STDMETHODIMP CAVIFileSynth::CreateInstance (LPUNKNOWN pUnkOuter, REFIID riid,  void * * ppvObj) {

  if (pUnkOuter) {
    _RPT1(0,"%p->CAVIFileSynth::CreateInstance() CLASS_E_NOAGGREGATION\n", this);
    return CLASS_E_NOAGGREGATION;
  }
  _RPT1(0,"%p->CAVIFileSynth::CreateInstance()\n", this);

  HRESULT hresult = Create(CLSID_CAVIFileSynth, riid, ppvObj);

  _RPT3(0,"%p->CAVIFileSynth::CreateInstance() result=0x%X, object=%p\n", this, hresult, *ppvObj);

  return hresult;
}

STDMETHODIMP CAVIFileSynth::LockServer (BOOL fLock) {
  _RPT2(0,"%p->CAVIFileSynth::LockServer(%u)\n", this, fLock);
  return S_OK;
}

///////////////////////////////////////////////////
//////////// IPersistFile

STDMETHODIMP CAVIFileSynth::GetClassID(LPCLSID lpClassID) {  // IPersist
  _RPT1(0,"%p->CAVIFileSynth::GetClassID()\n", this);

  if (!lpClassID) return E_POINTER;

  *lpClassID = CLSID_CAVIFileSynth;

  return S_OK;
}

STDMETHODIMP CAVIFileSynth::IsDirty() {
  _RPT1(0,"%p->CAVIFileSynth::IsDirty()\n", this);
  return S_FALSE;
}

STDMETHODIMP CAVIFileSynth::Load(LPCOLESTR lpszFileName, DWORD grfMode) {
  char filename[MAX_PATH];

  WideCharToMultiByte(AreFileApisANSI() ? CP_ACP : CP_OEMCP, 0, lpszFileName, -1, filename, sizeof filename, NULL, NULL); 

  _RPT3(0,"%p->CAVIFileSynth::Load(\"%s\", 0x%X)\n", this, filename, grfMode);

  return Open(filename, grfMode, lpszFileName);
}

STDMETHODIMP CAVIFileSynth::Save(LPCOLESTR lpszFileName, BOOL fRemember) {
  _RPT1(0,"%p->CAVIFileSynth::Save()\n", this);
  return E_FAIL;
}

STDMETHODIMP CAVIFileSynth::SaveCompleted(LPCOLESTR lpszFileName) {
  _RPT1(0,"%p->CAVIFileSynth::SaveCompleted()\n", this);
  return S_OK;
}

STDMETHODIMP CAVIFileSynth::GetCurFile(LPOLESTR *lplpszFileName) {
  _RPT1(0,"%p->CAVIFileSynth::GetCurFile()\n", this);

  if (lplpszFileName) *lplpszFileName = NULL;

  return E_FAIL;
}

///////////////////////////////////////////////////
/////// static local

HRESULT CAVIFileSynth::Create(const CLSID& rclsid, const IID& riid, void **ppv) {
  HRESULT hresult;

  //	_RPT0(0,"CAVIFileSynth::Create()\n");

  CAVIFileSynth* pAVIFileSynth = new(std::nothrow) CAVIFileSynth(rclsid);

  if (!pAVIFileSynth) return E_OUTOFMEMORY;

  hresult = pAVIFileSynth->QueryInterface(riid, ppv);
  pAVIFileSynth->Release();

  //	_RPT1(0,"CAVIFileSynth::Create() exit, result=0x%X\n", hresult);

  return hresult;
}

///////////////////////////////////////////////////
//////////// IUnknown

STDMETHODIMP CAVIFileSynth::QueryInterface(const IID& iid, void **ppv) {

  if (!ppv) {
    _RPT1(0,"%p->CAVIFileSynth::QueryInterface() E_POINTER\n", this);
    return E_POINTER;
  }

  _RPT1(0,"%p->CAVIFileSynth::QueryInterface() ", this);
  _RPT3(0,"{%08lx-%04x-%04x-", iid.Data1, iid.Data2, iid.Data3);
  _RPT4(0,"%02x%02x-%02x%02x", iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3]);
  _RPT4(0,"%02x%02x%02x%02x} (", iid.Data4[4], iid.Data4[5], iid.Data4[6], iid.Data4[7]);

  if (iid == IID_IUnknown) {
    *ppv = (IUnknown *)(IAVIFile *)this;
    _RPT0(0,"IUnknown)\n");
  } else if (iid == IID_IClassFactory) {
    *ppv = (IClassFactory *)this;
    _RPT0(0,"IClassFactory)\n");
  } else if (iid == IID_IPersist) {
    *ppv = (IPersist *)this;
    _RPT0(0,"IPersist)\n");
  } else if (iid == IID_IPersistFile) {
    *ppv = (IPersistFile *)this;
    _RPT0(0,"IPersistFile)\n");
  } else if (iid == IID_IAVIFile) {
    *ppv = (IAVIFile *)this;
    _RPT0(0,"IAVIFile)\n");
  } else if (iid == IID_IAvisynthClipInfo) {
    *ppv = (IAvisynthClipInfo *)this;
    _RPT0(0,"IAvisynthClipInfo)\n");
  } else {
    _RPT0(0,"unsupported!)\n");
    *ppv = NULL;
    return E_NOINTERFACE;
  }

  AddRef();

  return S_OK;
}

STDMETHODIMP_(ULONG) CAVIFileSynth::AddRef() {

  const int refs = InterlockedIncrement(&m_refs);
  InterlockedIncrement(&gRefCnt);

  _RPT3(0,"%p->CAVIFileSynth::AddRef() gRefCnt=%d, m_refs=%d\n", this, gRefCnt, refs);

  return refs;
}

STDMETHODIMP_(ULONG) CAVIFileSynth::Release() {

  const int refs = InterlockedDecrement(&m_refs);
  InterlockedDecrement(&gRefCnt);

  _RPT3(0,"%p->CAVIFileSynth::Release() gRefCnt=%d, m_refs=%d\n", this, gRefCnt, refs);

  if (!refs) delete this;
  return refs;
}

////////////////////////////////////////////////////////////////////////
//
//		CAVIStreamSynth
//
////////////////////////////////////////////////////////////////////////
//////////// IUnknown

STDMETHODIMP CAVIStreamSynth::QueryInterface(const IID& iid, void **ppv) {

  if (!ppv) {
    _RPT2(0,"%p->CAVIStreamSynth::QueryInterface() (%s) E_POINTER\n", this, sName);
    return E_POINTER;
  }

  _RPT2(0,"%p->CAVIStreamSynth::QueryInterface() (%s) ", this, sName);
  _RPT3(0,"{%08lx-%04x-%04x-", iid.Data1, iid.Data2, iid.Data3);
  _RPT4(0,"%02x%02x-%02x%02x", iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3]);
  _RPT4(0,"%02x%02x%02x%02x} (", iid.Data4[4], iid.Data4[5], iid.Data4[6], iid.Data4[7]);

  if (iid == IID_IUnknown) {
    *ppv = (IUnknown *)(IAVIStream *)this;
    _RPT0(0,"IUnknown)\n");
  } else if (iid == IID_IAVIStream) {
    *ppv = (IAVIStream *)this;
    _RPT0(0,"IAVIStream)\n");
  } else if (iid == IID_IAVIStreaming) {
    *ppv = (IAVIStreaming *)this;
    _RPT0(0,"IAVIStreaming)\n");
  } else {
    _RPT0(0,"unsupported!)\n");
    *ppv = NULL;
    return E_NOINTERFACE;
  }

  AddRef();

  return S_OK;
}

STDMETHODIMP_(ULONG) CAVIStreamSynth::AddRef() {

  const int refs = InterlockedIncrement(&m_refs);
  InterlockedIncrement(&gRefCnt);

  _RPT4(0,"%p->CAVIStreamSynth::AddRef() (%s) gRefCnt=%d, m_refs=%d\n", this, sName, gRefCnt, refs);

  return refs;
}

STDMETHODIMP_(ULONG) CAVIStreamSynth::Release() {

  const int refs = InterlockedDecrement(&m_refs);
  InterlockedDecrement(&gRefCnt);

  _RPT4(0,"%p->CAVIStreamSynth::Release() (%s) gRefCnt=%d, m_refs=%d\n", this, sName, gRefCnt, refs);

  if (!refs) delete this;
  return refs;
}

////////////////////////////////////////////////////////////////////////
//
//		CAVIFileSynth
//
////////////////////////////////////////////////////////////////////////
//////////// IAVIFile

STDMETHODIMP CAVIFileSynth::CreateStream(PAVISTREAM *ppStream, AVISTREAMINFOW *psi) {
  _RPT1(0,"%p->CAVIFileSynth::CreateStream()\n", this);
  *ppStream = NULL;
  return S_OK;//AVIERR_READONLY;
}

STDMETHODIMP CAVIFileSynth::EndRecord() {
  _RPT1(0,"%p->CAVIFileSynth::EndRecord()\n", this);
  return AVIERR_READONLY;
}

STDMETHODIMP CAVIFileSynth::Save(LPCSTR szFile, AVICOMPRESSOPTIONS FAR *lpOptions,
                                 AVISAVECALLBACK lpfnCallback) {
                                   _RPT1(0,"%p->CAVIFileSynth::Save()\n", this);
                                   return AVIERR_READONLY;
}

STDMETHODIMP CAVIFileSynth::ReadData(DWORD fcc, LPVOID lp, LONG *lpcb) {
  _RPT1(0,"%p->CAVIFileSynth::ReadData()\n", this);
  return AVIERR_NODATA;
}

STDMETHODIMP CAVIFileSynth::WriteData(DWORD fcc, LPVOID lpBuffer, LONG cbBuffer) {
  _RPT1(0,"%p->CAVIFileSynth::WriteData()\n", this);
  return AVIERR_READONLY;
}

STDMETHODIMP CAVIFileSynth::DeleteStream(DWORD fccType, LONG lParam) {
  _RPT1(0,"%p->CAVIFileSynth::DeleteStream()\n", this);
  return AVIERR_READONLY;
}


///////////////////////////////////////////////////
/////// local

CAVIFileSynth::CAVIFileSynth(const CLSID& rclsid) {
  _RPT1(0,"%p->CAVIFileSynth::CAVIFileSynth()\n", this);

  m_refs = 0;
  AddRef();

  szScriptName = NULL;
  env = NULL;
  error_msg = NULL;

  VDubPlanarHack = false;
  AVIPadScanlines = false;

  InitializeCriticalSection(&cs_filter_graph);
}

CAVIFileSynth::~CAVIFileSynth() {
  _RPT2(0,"%p->CAVIFileSynth::~CAVIFileSynth(), gRefCnt = %d\n", this, gRefCnt);

  Lock();

  delete[] szScriptName;

  filter_graph = 0;

  if (env) env->DeleteScriptEnvironment();
  env = NULL;

  DeleteCriticalSection(&cs_filter_graph);
}


STDMETHODIMP CAVIFileSynth::Open(LPCSTR szFile, UINT mode, LPCOLESTR lpszFileName) {

  //	_RPT3(0,"%p->CAVIFileSynth::Open(\"%s\", 0x%08lx)\n", this, szFile, mode);

  if (mode & (OF_CREATE|OF_WRITE))
    return E_FAIL;

  if (env) env->DeleteScriptEnvironment();   // just in case
  env = NULL;
  filter_graph = 0;
  vi = NULL;

  szScriptName = new(std::nothrow) char[lstrlen(szFile)+1];
  if (!szScriptName)
    return AVIERR_MEMORY;
  lstrcpy(szScriptName, szFile);

  return S_OK;
}

bool CAVIFileSynth::DelayInit() {

  Lock();

  bool result = DelayInit2();

  Unlock();

  return result;
}

bool CAVIFileSynth::DelayInit2() {
  // _RPT1(0,"Original: 0x%.4x\n", _control87( 0, 0 ) );
#ifdef X86_32
  int fp_state = _control87( 0, 0 );
  _control87( FP_STATE, 0xffffffff );
#endif
  if (szScriptName)
  {
#ifndef _DEBUG
    try {
#endif
      try {
        // create a script environment and load the script into it
        env = CreateScriptEnvironment2();
        if (!env) return false;
      }
      catch (const AvisynthError &error) {
        error_msg = error.msg;
        return false;
      }
      try {
        AVSValue return_val = env->Invoke("Import", szScriptName);
        // store the script's return value (a video clip)
        if (return_val.IsClip()) {
          filter_graph = return_val.AsClip();

          // Allow WAVE_FORMAT_IEEE_FLOAT audio output
          const bool AllowFloatAudio = env->GetVar(VARNAME_AllowFloatAudio, false);

          if (!AllowFloatAudio && filter_graph->GetVideoInfo().IsSampleType(SAMPLE_FLOAT)) // Ensure samples are int     
            filter_graph = env->Invoke("ConvertAudioTo16bit", AVSValue(&return_val, 1)).AsClip();

          filter_graph = env->Invoke("Cache", AVSValue(filter_graph)).AsClip();

          // Tune top-level cache for encoding
          filter_graph->SetCacheHints(CACHE_SET_MIN_CAPACITY, 0);
          filter_graph->SetCacheHints(CACHE_SET_MAX_CAPACITY, 0);

          // Alternate sample values for editors:
          //filter_graph->SetCacheHints(CACHE_SET_MIN_CAPACITY, 10);
          //filter_graph->SetCacheHints(CACHE_SET_MAX_CAPACITY, 100);
        }
        else if (return_val.IsBool())
          env->ThrowError("The script's return value was not a video clip, (Is a bool, %s).", return_val.AsBool() ? "True" : "False");
        else if (return_val.IsInt())
          env->ThrowError("The script's return value was not a video clip, (Is an int, %d).", return_val.AsInt());
        else if (return_val.IsFloat())
          env->ThrowError("The script's return value was not a video clip, (Is a float, %f).", return_val.AsFloat());
        else if (return_val.IsString())
          env->ThrowError("The script's return value was not a video clip, (Is a string, %s).", return_val.AsString());
        else if (return_val.IsArray())
          env->ThrowError("The script's return value was not a video clip, (Is an array[%d]).", return_val.ArraySize());
        else if (!return_val.Defined())
          env->ThrowError("The script's return value was not a video clip, (Is the undefined value).");
        else
          env->ThrowError("The script's return value was not a video clip, (The type is unknown).");

        if (!filter_graph)
          env->ThrowError("The returned video clip was nil (this is a bug)");

        // get information about the clip
        vi = &filter_graph->GetVideoInfo();

        // Hack YV16 and YV24 chroma plane order for old VDub's
        VDubPlanarHack = env->GetVar(VARNAME_VDubPlanarHack, false);

        // Option to have scanlines mod4 padded in all pixel formats
        AVIPadScanlines = env->GetVar(VARNAME_AVIPadScanlines, false);

      }
      catch (const AvisynthError &error) {
        error_msg = error.msg;
        try {
          AVSValue args[2] = { error.msg, 0xff3333 };
          static const char* const arg_names[2] = { 0, "text_color" };
          filter_graph = env->Invoke("MessageClip", AVSValue(args, 2), arg_names).AsClip();
          vi = &filter_graph->GetVideoInfo();
        }
        catch (const AvisynthError&) {
          filter_graph = 0;
        }
      }

      delete[] szScriptName;
      szScriptName = NULL;
      _clear87();
#ifdef X86_32
      _mm_empty();
      _control87( fp_state, 0xffffffff );
#endif
      return true;
#ifndef _DEBUG
    }
    catch (...) {
      _RPT0(1,"DelayInit() caught general exception!\n");
      _clear87();
#ifdef X86_32
      _mm_empty();
      _control87( fp_state, 0xffffffff );
#endif
      return false;
    }
#endif
  } else {
    _clear87();
#ifdef X86_32
    _mm_empty();
    _control87( fp_state, 0xffffffff );
#endif
    return (env && filter_graph && vi);
  }
}


void CAVIFileSynth::MakeErrorStream(const char* msg) {
  error_msg = msg;
  filter_graph = Create_MessageClip(msg, vi->width, vi->height, vi->pixel_type, false, 0xFF3333, 0, 0, env);
}

void CAVIFileSynth::Lock() {

  EnterCriticalSection(&cs_filter_graph);

}

void CAVIFileSynth::Unlock() {

  LeaveCriticalSection(&cs_filter_graph);

}

///////////////////////////////////////////////////
//////////// IAVIFile

STDMETHODIMP CAVIFileSynth::Info(AVIFILEINFOW *pfi, LONG lSize) {

  _RPT2(0,"%p->CAVIFileSynth::Info(pfi, %d)\n", this, lSize);

  if (!pfi) return E_POINTER;

  if (!DelayInit()) return E_FAIL;

  AVIFILEINFOW afi;

  memset(&afi, 0, sizeof(afi));

  afi.dwMaxBytesPerSec	= 0;
  afi.dwFlags				= AVIFILEINFO_HASINDEX | AVIFILEINFO_ISINTERLEAVED;
  afi.dwCaps				= AVIFILECAPS_CANREAD | AVIFILECAPS_ALLKEYFRAMES | AVIFILECAPS_NOCOMPRESSION;

  int nrStreams=0;
  if (vi->HasVideo()==true)	nrStreams=1;
  if (vi->HasAudio()==true)	nrStreams++;

  afi.dwStreams				= nrStreams;
  afi.dwSuggestedBufferSize	= 0;
  afi.dwWidth					= vi->width;
  afi.dwHeight				= vi->height;
  afi.dwEditCount				= 0;

  afi.dwRate					= vi->fps_numerator;
  afi.dwScale					= vi->fps_denominator;
  afi.dwLength				= vi->num_frames;

  wcscpy(afi.szFileType, L"Avisynth");

  // Maybe should return AVIERR_BUFFERTOOSMALL for lSize < sizeof(afi)
  memset(pfi, 0, lSize);
  memcpy(pfi, &afi, min(size_t(lSize), sizeof(afi)));
  return S_OK;
}

static inline char BePrintable(int ch) {
  ch &= 0xff;
  return (char)(isprint(ch) ? ch : '.');
}


STDMETHODIMP CAVIFileSynth::GetStream(PAVISTREAM *ppStream, DWORD fccType, LONG lParam) {
  CAVIStreamSynth *casr;
  char fcc[5];

  fcc[0] = BePrintable(fccType      );
  fcc[1] = BePrintable(fccType >>  8);
  fcc[2] = BePrintable(fccType >> 16);
  fcc[3] = BePrintable(fccType >> 24);
  fcc[4] = 0;

  _RPT4(0,"%p->CAVIFileSynth::GetStream(*, %08x(%s), %ld)\n", this, fccType, fcc, lParam);

  if (!DelayInit()) return E_FAIL;

  *ppStream = NULL;

  if (!fccType) 
  {
    // Maybe an Option to set the order of stream discovery
    if ((lParam==0) && (vi->HasVideo()) )
      fccType = streamtypeVIDEO;
    else 
      if ( ((lParam==1) && (vi->HasVideo())) ||  ((lParam==0) && vi->HasAudio()) )
      {
        lParam=0;
        fccType = streamtypeAUDIO;
      }
  }

  if (lParam > 0) return AVIERR_NODATA;

  if (fccType == streamtypeVIDEO) {
    if (!vi->HasVideo())
      return AVIERR_NODATA;

    if ((casr = new(std::nothrow) CAVIStreamSynth(this, false)) == 0)
      return AVIERR_MEMORY;

    *ppStream = (IAVIStream *)casr;

  } else if (fccType == streamtypeAUDIO) {
    if (!vi->HasAudio())
      return AVIERR_NODATA;

    if ((casr = new(std::nothrow) CAVIStreamSynth(this, true)) == 0)
      return AVIERR_MEMORY;

    *ppStream = (IAVIStream *)casr;
  } else
    return AVIERR_NODATA;

  return S_OK;
}


////////////////////////////////////////////////////////////////////////
//////////// IAvisynthClipInfo

int __stdcall CAVIFileSynth::GetError(const char** ppszMessage) {
  if (!DelayInit() && !error_msg)
    error_msg = "Avisynth: script open failed!";

  if (ppszMessage)
    *ppszMessage = error_msg;
  return !!error_msg;
}

bool __stdcall CAVIFileSynth::GetParity(int n) {
  if (!DelayInit())
    return false;
  return filter_graph->GetParity(n);
}

bool __stdcall CAVIFileSynth::IsFieldBased() {
  if (!DelayInit())
    return false;
  return vi->IsFieldBased();
}


////////////////////////////////////////////////////////////////////////
//
//		CAVIStreamSynth
//
////////////////////////////////////////////////////////////////////////
//////////// IAVIStreaming

STDMETHODIMP CAVIStreamSynth::Begin(LONG lStart, LONG lEnd, LONG lRate) {
  _RPT5(0,"%p->CAVIStreamSynth::Begin(%ld, %ld, %ld) (%s)\n", this, lStart, lEnd, lRate, sName);
  return S_OK;
}

STDMETHODIMP CAVIStreamSynth::End() {
  _RPT2(0,"%p->CAVIStreamSynth::End() (%s)\n", this, sName);
  return S_OK;
}

//////////// IAVIStream

STDMETHODIMP CAVIStreamSynth::Create(LPARAM lParam1, LPARAM lParam2) {
  _RPT1(0,"%p->CAVIStreamSynth::Create()\n", this);
  return AVIERR_READONLY;
}

STDMETHODIMP CAVIStreamSynth::Delete(LONG lStart, LONG lSamples) {
  _RPT1(0,"%p->CAVIStreamSynth::Delete()\n", this);
  return AVIERR_READONLY;
}

STDMETHODIMP CAVIStreamSynth::ReadData(DWORD fcc, LPVOID lp, LONG *lpcb) {
  _RPT1(0,"%p->CAVIStreamSynth::ReadData()\n", this);
  return AVIERR_NODATA;
}

STDMETHODIMP CAVIStreamSynth::SetFormat(LONG lPos, LPVOID lpFormat, LONG cbFormat) {
  _RPT1(0,"%p->CAVIStreamSynth::SetFormat()\n", this);
  return AVIERR_READONLY;
}

STDMETHODIMP CAVIStreamSynth::WriteData(DWORD fcc, LPVOID lpBuffer, LONG cbBuffer) {
  _RPT1(0,"%p->CAVIStreamSynth::WriteData()\n", this);
  return AVIERR_READONLY;
}

STDMETHODIMP CAVIStreamSynth::SetInfo(AVISTREAMINFOW *psi, LONG lSize) {
  _RPT1(0,"%p->CAVIStreamSynth::SetInfo()\n", this);
  return AVIERR_READONLY;
}

////////////////////////////////////////////////////////////////////////
//////////// local

CAVIStreamSynth::CAVIStreamSynth(CAVIFileSynth *parentPtr, bool isAudio) {

  sName = isAudio ? "audio" : "video";

  _RPT2(0,"%p->CAVIStreamSynth(%s)\n", this, sName);

  m_refs = 0; AddRef();

  parent			= parentPtr;
  fAudio			= isAudio;

  parent->AddRef();
}

CAVIStreamSynth::~CAVIStreamSynth() {
  _RPT3(0,"%p->~CAVIStreamSynth() (%s), gRefCnt = %d\n", this, sName, gRefCnt);

  if (parent)
    parent->Release();
}

////////////////////////////////////////////////////////////////////////
//////////// IAVIStream

STDMETHODIMP_(LONG) CAVIStreamSynth::Info(AVISTREAMINFOW *psi, LONG lSize) {
  _RPT4(0,"%p->CAVIStreamSynth::Info(%p, %ld) (%s)\n", this, psi, lSize, sName);

  if (!psi) return E_POINTER;

  AVISTREAMINFOW asi;

  const VideoInfo* const vi = parent->vi;

  memset(&asi, 0, sizeof(asi));
  asi.fccType = fAudio ? streamtypeAUDIO : streamtypeVIDEO;
  asi.dwQuality = DWORD(-1);
  if (fAudio) {
    asi.fccHandler = 0;
    int bytes_per_sample = vi->BytesPerAudioSample();
    asi.dwScale = bytes_per_sample;
    asi.dwRate = vi->audio_samples_per_second * bytes_per_sample;
    asi.dwLength = (unsigned int)vi->num_audio_samples;
    asi.dwSampleSize = bytes_per_sample;
    wcscpy(asi.szName, L"Avisynth audio #1");
  } else {
    const int image_size = parent->ImageSize();
    asi.fccHandler = 'UNKN';
    if (vi->IsRGB()) 
      asi.fccHandler = ' BID';
    else if (vi->IsYUY2())
      asi.fccHandler = '2YUY';
    else if (vi->IsYV12())
      asi.fccHandler = '21VY'; 
    else if (vi->IsY8())
      asi.fccHandler = '008Y'; 
    else if (vi->IsYV24())
      asi.fccHandler = '42VY'; 
    else if (vi->IsYV16()) 
      asi.fccHandler = '61VY'; 
    else if (vi->IsYV411()) 
      asi.fccHandler = 'B14Y'; 
    else {
      _ASSERT(FALSE);
    }

    asi.dwScale = vi->fps_denominator;
    asi.dwRate = vi->fps_numerator;
    asi.dwLength = vi->num_frames;
    asi.rcFrame.right = vi->width;
    asi.rcFrame.bottom = vi->height;
    asi.dwSampleSize = image_size;
    asi.dwSuggestedBufferSize = image_size;
    wcscpy(asi.szName, L"Avisynth video #1");
  }

  // Maybe should return AVIERR_BUFFERTOOSMALL for lSize < sizeof(asi)
  memset(psi, 0, lSize);
  memcpy(psi, &asi, min(size_t(lSize), sizeof(asi)));
  return S_OK;
}

STDMETHODIMP_(LONG) CAVIStreamSynth::FindSample(LONG lPos, LONG lFlags) {
  //	_RPT3(0,"%p->CAVIStreamSynth::FindSample(%ld, %08lx)\n", this, lPos, lFlags);

  if (lFlags & FIND_FORMAT)
    return -1;

  if (lFlags & FIND_FROM_START)
    return 0;

  return lPos;
}


////////////////////////////////////////////////////////////////////////
//////////// local

int CAVIFileSynth::ImageSize() {
  int image_size;

  if (vi->IsRGB() || vi->IsYUY2() || vi->IsY8() || vi->IsColorSpace(VideoInfo::CS_Y16) || vi->IsColorSpace(VideoInfo::CS_Y32) || AVIPadScanlines) {
    image_size = vi->BMPSize();
  }
  else { // Packed size
    image_size = vi->RowSize(PLANAR_U);
    if (image_size) {
      image_size  *= vi->height;
      image_size >>= vi->GetPlaneHeightSubsampling(PLANAR_U);
      image_size  *= 2;
    }
    image_size += vi->RowSize(PLANAR_Y) * vi->height;
  }
  return image_size;
}


void CAVIStreamSynth::ReadFrame(void* lpBuffer, int n) {
  PVideoFrame frame = parent->filter_graph->GetFrame(n, parent->env);
  if (!frame)
    parent->env->ThrowError("Avisynth error: generated video frame was nil (this is a bug)");

  VideoInfo vi = parent->filter_graph->GetVideoInfo();
  const int pitch    = frame->GetPitch();
  const int row_size = frame->GetRowSize();
  const int height   = frame->GetHeight();

  int out_pitch;
  int out_pitchUV;

  // BMP scanlines are dword-aligned
  if (vi.IsRGB() || vi.IsYUY2() || vi.IsY8() || vi.IsColorSpace(VideoInfo::CS_Y16) || vi.IsColorSpace(VideoInfo::CS_Y32) || parent->AVIPadScanlines) {
    out_pitch = (row_size+3) & ~3;
    out_pitchUV = (frame->GetRowSize(PLANAR_U)+3) & ~3;
  }
  // Planar scanlines are packed
  else {
    out_pitch = row_size;
    out_pitchUV = frame->GetRowSize(PLANAR_U);
  }

  // Set default VFW output plane order.
  int plane1 = PLANAR_V;
  int plane2 = PLANAR_U;

  // Old VDub wants YUV for YV24 and YV16 and YVU for YV12.
  if (parent->VDubPlanarHack && !vi.IsYV12()) {
    plane1 = PLANAR_U;
    plane2 = PLANAR_V;
  }

  parent->env->BitBlt((BYTE*)lpBuffer, out_pitch, frame->GetReadPtr(), pitch, row_size, height);

  parent->env->BitBlt((BYTE*)lpBuffer + (out_pitch*height),
    out_pitchUV,             frame->GetReadPtr(plane1),
    frame->GetPitch(plane1), frame->GetRowSize(plane1),
    frame->GetHeight(plane1) );

  parent->env->BitBlt((BYTE*)lpBuffer + (out_pitch*height + frame->GetHeight(plane1)*out_pitchUV),
    out_pitchUV,             frame->GetReadPtr(plane2),
    frame->GetPitch(plane2), frame->GetRowSize(plane2),
    frame->GetHeight(plane2) );
}


////////////////////////////////////////////////////////////////////////
//////////// IAVIStream

STDMETHODIMP CAVIStreamSynth::Read(LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, LONG *plBytes, LONG *plSamples) {

#ifdef  X86_32
  // TODO: Does this need 64-bit porting?
  __asm { // Force compiler to protect these registers!
    mov ebx,ebx;
    mov esi,esi;
    mov edi,edi;
  }
#endif

  parent->Lock();

  HRESULT result = Read2(lStart, lSamples, lpBuffer, cbBuffer, plBytes, plSamples);

  parent->Unlock();

  return result;
}

HRESULT CAVIStreamSynth::Read2(LONG lStart, LONG lSamples, LPVOID lpBuffer, LONG cbBuffer, LONG *plBytes, LONG *plSamples) {

  //  _RPT3(0,"%p->CAVIStreamSynth::Read(%ld samples at %ld)\n", this, lSamples, lStart);
  //  _RPT2(0,"\tbuffer: %ld bytes at %p\n", cbBuffer, lpBuffer);
#ifdef X86_32
  int fp_state = _control87( 0, 0 );
  _control87( FP_STATE, 0xffffffff );
#endif

  const VideoInfo* const vi = parent->vi;

  if (fAudio) {
    // buffer overflow patch -- Avery Lee - Mar 2006
    if (lSamples == AVISTREAMREAD_CONVENIENT)
      lSamples = (int)vi->AudioSamplesFromFrames(1);

    if (__int64(lStart)+lSamples > vi->num_audio_samples) {
      lSamples = (int)(vi->num_audio_samples - lStart);
      if (lSamples < 0) lSamples = 0;
    }

    int bytes = (int)vi->BytesFromAudioSamples(lSamples);
    if (lpBuffer && bytes > cbBuffer) {
      lSamples = (int)vi->AudioSamplesFromBytes(cbBuffer);
      bytes = (int)vi->BytesFromAudioSamples(lSamples);
    }
    if (plBytes) *plBytes = bytes;
    if (plSamples) *plSamples = lSamples;
    if (!lpBuffer || !lSamples)
      return S_OK;

  } else {
    if (lStart >= vi->num_frames) {
      if (plSamples) *plSamples = 0;
      if (plBytes) *plBytes = 0;
      return S_OK;
    }

    const int image_size = parent->ImageSize();
    if (plSamples) *plSamples = 1;
    if (plBytes) *plBytes = image_size;

    if (!lpBuffer) {
      return S_OK;
    } else if (cbBuffer < image_size) {
      //      _RPT1(0,"\tBuffer too small; should be %ld samples\n", image_size);
      return AVIERR_BUFFERTOOSMALL;
    }
  }

#ifndef _DEBUG
  try {
    try {
#endif
      SehGuard seh_guard;

      if (fAudio)
        parent->filter_graph->GetAudio(lpBuffer, lStart, lSamples, parent->env);
      else
        ReadFrame(lpBuffer, lStart);

#ifndef _DEBUG
    }
    catch (AvisynthError &error) {
      parent->MakeErrorStream(error.msg);
      throw;
    }
    catch (SehException &seh) {
      char buf[256];
      _snprintf(buf, 255, "CAVIStreamSynth: %s at 0x%p", seh.m_msg, seh.m_addr);
      parent->MakeErrorStream(parent->env->SaveString(buf));
      throw;
    }
    catch (...) {
      parent->MakeErrorStream("CAVIStreamSynth: unhandled C++ exception");
      throw;
    }
  }
  catch (...) {
    _clear87();
#ifdef X86_32
    _mm_empty();
    _control87( fp_state, 0xffffffff );
#endif
    return E_FAIL;
  }
#endif
  _clear87();
#ifdef X86_32
  _mm_empty();
  _control87( fp_state, 0xffffffff );
#endif
  return S_OK;
}

STDMETHODIMP CAVIStreamSynth::ReadFormat(LONG lPos, LPVOID lpFormat, LONG *lpcbFormat) {
  _RPT2(0,"%p->CAVIStreamSynth::ReadFormat() (%s)\n", this, sName);

  if (!lpcbFormat) return E_POINTER;

  const bool UseWaveExtensible = parent->env->GetVar(VARNAME_UseWaveExtensible, false);

  if (!lpFormat) {
    *lpcbFormat = fAudio ? ( UseWaveExtensible ? sizeof(WAVEFORMATEXTENSIBLE)
      : sizeof(WAVEFORMATEX) )
      : sizeof(BITMAPINFOHEADER);
    return S_OK;
  }

  memset(lpFormat, 0, *lpcbFormat);

  const VideoInfo* const vi = parent->vi;

  if (fAudio) {
    if (UseWaveExtensible) {  // Use WAVE_FORMAT_EXTENSIBLE audio output format 
#ifndef KSDATAFORMAT_SUBTYPE_PCM	// VS2005 does not define this
      const GUID KSDATAFORMAT_SUBTYPE_PCM       = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
#endif
#ifndef KSDATAFORMAT_SUBTYPE_IEEE_FLOAT	// VS2005 does not define this
      const GUID KSDATAFORMAT_SUBTYPE_IEEE_FLOAT= {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
#endif
      WAVEFORMATEXTENSIBLE wfxt;

      memset(&wfxt, 0, sizeof(wfxt));
      wfxt.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
      wfxt.Format.nChannels = (WORD)vi->AudioChannels();
      wfxt.Format.nSamplesPerSec = vi->SamplesPerSecond();
      wfxt.Format.wBitsPerSample = (WORD)(vi->BytesPerChannelSample() * 8);
      wfxt.Format.nBlockAlign = (WORD)vi->BytesPerAudioSample();
      wfxt.Format.nAvgBytesPerSec = wfxt.Format.nSamplesPerSec * wfxt.Format.nBlockAlign;
      wfxt.Format.cbSize = sizeof(wfxt) - sizeof(wfxt.Format);
      wfxt.Samples.wValidBitsPerSample = wfxt.Format.wBitsPerSample;

      const int SpeakerMasks[9] = { 0,
        0x00004, // 1   -- -- Cf
        0x00003, // 2   Lf Rf
        0x00007, // 3   Lf Rf Cf
        0x00033, // 4   Lf Rf -- -- Lr Rr
        0x00037, // 5   Lf Rf Cf -- Lr Rr
        0x0003F, // 5.1 Lf Rf Cf Sw Lr Rr
        0x0013F, // 6.1 Lf Rf Cf Sw Lr Rr -- -- Cr
        0x0063F, // 7.1 Lf Rf Cf Sw Lr Rr -- -- -- Ls Rs
      };
      wfxt.dwChannelMask = (unsigned)vi->AudioChannels() <= 8 ? SpeakerMasks[vi->AudioChannels()]
      : (unsigned)vi->AudioChannels() <=18 ? DWORD(-1) >> (32-vi->AudioChannels())
        : SPEAKER_ALL;

      unsigned int userChannelMask = (unsigned)(parent->env->GetVar(VARNAME_dwChannelMask, 0));
      if (userChannelMask != 0)
        wfxt.dwChannelMask = userChannelMask;

      wfxt.SubFormat = vi->IsSampleType(SAMPLE_FLOAT) ? KSDATAFORMAT_SUBTYPE_IEEE_FLOAT : KSDATAFORMAT_SUBTYPE_PCM;
      *lpcbFormat = min(*lpcbFormat, (LONG)sizeof(wfxt));
      memcpy(lpFormat, &wfxt, size_t(*lpcbFormat));
    }
    else {
      WAVEFORMATEX wfx;
      memset(&wfx, 0, sizeof(wfx));
      wfx.wFormatTag = vi->IsSampleType(SAMPLE_FLOAT) ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
      wfx.nChannels = (WORD)vi->AudioChannels();
      wfx.nSamplesPerSec = vi->SamplesPerSecond();
      wfx.wBitsPerSample = (WORD)(vi->BytesPerChannelSample() * 8);
      wfx.nBlockAlign = (WORD)vi->BytesPerAudioSample();
      wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
      *lpcbFormat = min(*lpcbFormat, (LONG)sizeof(wfx));
      memcpy(lpFormat, &wfx, size_t(*lpcbFormat));
    }
  } else {
    BITMAPINFOHEADER bi;
    memset(&bi, 0, sizeof(bi));
    bi.biSize = sizeof(bi);
    bi.biWidth = vi->width;
    bi.biHeight = vi->height;
    bi.biPlanes = 1;
    bi.biBitCount = (WORD)vi->BitsPerPixel();

    if (vi->IsRGB()) 
      bi.biCompression = BI_RGB;
    else if (vi->IsYUY2())
      bi.biCompression = '2YUY';
    else if (vi->IsYV12())
      bi.biCompression = '21VY';
    else if (vi->IsY8())
      bi.biCompression = '008Y'; 
    else if (vi->IsYV24())
      bi.biCompression = '42VY'; 
    else if (vi->IsYV16()) 
      bi.biCompression = '61VY'; 
    else if (vi->IsYV411()) 
      bi.biCompression = 'B14Y'; 
    else {
      _ASSERT(FALSE);
    }

    bi.biSizeImage = parent->ImageSize();
    *lpcbFormat = min(*lpcbFormat, (LONG)sizeof(bi));
    memcpy(lpFormat, &bi, size_t(*lpcbFormat));
  }
  return S_OK;
}

STDMETHODIMP CAVIStreamSynth::Write(LONG lStart, LONG lSamples, LPVOID lpBuffer,
                                    LONG cbBuffer, DWORD dwFlags, LONG FAR *plSampWritten, 
                                    LONG FAR *plBytesWritten) {

                                      _RPT1(0,"%p->CAVIStreamSynth::Write()\n", this);

                                      return AVIERR_READONLY;
}

