#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <string_view>
#include "MinHook.h"
#include "runtime_addresses.h"
#include "font_overlay.h"
#include "help_overlay.h"

struct Translation { const char* en; const char* zh; };
#include "runtime_table.h"
// The game's MSVC hxcpp String is a 16-byte pair. Calls use explicit sret in RDX.
struct HxString { int length; int padding; const char* bytes; };
using Setter = HxString* (__fastcall*)(void*,HxString*,HxString*);
using Marker = void (__fastcall*)(void*,void*);
static Setter originalText, originalHtml, originalStarText;
static Marker originalMark, originalVisit;
static uintptr_t base;
static bool chinese=true, replaying=false;
static thread_local int insideSetter=0;
static DWORD gameThread=0;
static HMODULE selfModule;
static wchar_t configPath[MAX_PATH];
struct Candidate { const Translation* item; size_t length; };
static std::unordered_map<std::string,std::string> exactTranslations;
static std::vector<Candidate> candidates[256];
constexpr std::string_view carryLimitToken = "$CARRYLIMIT";
struct CarryLimitTranslation {
    std::string_view prefix, suffix, chinesePrefix, chineseSuffix;
};
static std::vector<CarryLimitTranslation> carryLimitTranslations;

// Allocated in the mod heap and flagged as constant for hxcpp's GC. A buffer remains
// alive while any game String might still point at it; reuse identical text forever.
struct Constant {
    std::string text;
    std::unique_ptr<unsigned char[]> storage;
    HxString value;
    Constant(const std::string& s):text(s),storage(new unsigned char[s.size()+9]) {
        uint32_t hash=0;for(unsigned char c:s)hash=hash*223+c;
        memcpy(storage.get(),&hash,4);uint32_t flags=0x80100000;memcpy(storage.get()+4,&flags,4);
        memcpy(storage.get()+8,s.c_str(),s.size()+1);
        value={int(s.size()),0,reinterpret_cast<const char*>(storage.get()+8)};
    }
};
static std::vector<std::unique_ptr<Constant>> constants;
static Constant* constant(const std::string& s) {
    for(auto& p:constants)if(p->text==s)return p.get();
    constants.emplace_back(new Constant(s));return constants.back().get();
}
struct Entry { void* object; int kind; Constant* english; Constant* translated; };
static std::vector<std::unique_ptr<Entry>> fields;

static bool word(unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';}
static std::string translatePlain(const std::string& s) {
    auto exact=exactTranslations.find(s);if(exact!=exactTranslations.end())return exact->second;
    // The tutorial replaces $CARRYLIMIT before handing text to the renderer.
    // Match the complete template (or a controller prompt up to its icon).
    const std::string_view input(s);
    for (const auto& rule : carryLimitTranslations) {
        if (input.substr(0, rule.prefix.size()) != rule.prefix) continue;
        const auto start = rule.prefix.size();
        auto end = start;
        while (end < input.size() && input[end] >= '0' && input[end] <= '9') ++end;
        if (start == end || input.substr(end) != rule.suffix) continue;
        std::string translated(rule.chinesePrefix);
        translated.append(input.substr(start, end - start));
        translated.append(rule.chineseSuffix);
        return translated;
    }
    bool hasEnglish=false;for(unsigned char c:s)if((c>='a'&&c<='z')||(c>='A'&&c<='Z')){hasEnglish=true;break;}
    if(!hasEnglish)return s;
    // Longest source first, and boundaries protect identifiers and people's names.
    std::string result;size_t i=0;
    while(i<s.size()) {
        const Translation* match=nullptr;size_t length=0;
        for(const auto& c:candidates[(unsigned char)s[i]]) {
            const auto& t=*c.item;size_t n=c.length;
            if(n<3||n<=length||i+n>s.size()||memcmp(s.data()+i,t.en,n))continue;
            if(word((unsigned char)t.en[0])&&i&&word((unsigned char)s[i-1]))continue;
            if(word((unsigned char)t.en[n-1])&&i+n<s.size()&&word((unsigned char)s[i+n]))continue;
            match=&t;length=n;
        }
        if(match){result+=match->zh;i+=length;}else result+=s[i++];
    }
    return result;
}
static std::string replaceAll(std::string s,const char* a,const char* b) {
    size_t i=0,n=strlen(a),m=strlen(b);while((i=s.find(a,i))!=std::string::npos){s.replace(i,n,b);i+=m;}return s;
}
static std::string decode(std::string s){s=replaceAll(s,"&apos;","'");s=replaceAll(s,"&quot;","\"");s=replaceAll(s,"&lt;","<");s=replaceAll(s,"&gt;",">");return replaceAll(s,"&amp;","&");}
static std::string encode(std::string s){s=replaceAll(s,"&","&amp;");s=replaceAll(s,"<","&lt;");return replaceAll(s,">","&gt;");}
static std::string translate(const std::string& s,bool html) {
    if(!html)return translatePlain(s);
    std::string out;size_t i=0;
    while(i<s.size()) {
        if(s[i]=='<') {size_t e=s.find('>',i);if(e==std::string::npos){out+=s.substr(i);break;}out+=s.substr(i,e-i+1);i=e+1;}
        else {size_t e=s.find('<',i);if(e==std::string::npos)e=s.size();std::string segment=s.substr(i,e-i),raw=decode(segment),z=translatePlain(raw);out+=(z==raw?segment:encode(z));i=e;}
    }
    return out;
}
static void log(const char* text) {
    wchar_t path[MAX_PATH];GetModuleFileNameW(selfModule,path,MAX_PATH);wchar_t* slash=wcsrchr(path,L'\\');if(!slash)return;wcscpy(slash+1,L"wilmzh.log");
    HANDLE f=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f!=INVALID_HANDLE_VALUE){DWORD n;WriteFile(f,text,(DWORD)strlen(text),&n,nullptr);WriteFile(f,"\r\n",2,&n,nullptr);CloseHandle(f);}
}
static Setter setter(int kind){return kind==1?originalHtml:kind==2?originalStarText:originalText;}
static HxString* setText(int kind,void* object,HxString* result,HxString* input) {
    if(replaying||insideSetter||!input||!input->bytes||input->length<0||input->length>50000)return setter(kind)(object,result,input);
    gameThread=GetCurrentThreadId();
    std::string en(input->bytes,input->length),zh=translate(en,kind==1);
    Entry* e=nullptr;for(auto& p:fields)if(p->object==object){e=p.get();break;}
    // Cache only translated fields and fields already tracked, keeping the footprint small.
    if(zh!=en||e) {
        if(!e){fields.emplace_back(new Entry{object,kind,nullptr,nullptr});e=fields.back().get();}
        e->kind=kind;e->english=constant(en);e->translated=constant(zh);
        HxString value=(chinese?e->translated:e->english)->value;
        ++insideSetter;setter(kind)(object,result,&value);--insideSetter;
        // Preserve the property's English return value for game logic.
        *result=e->english->value;return result;
    }
    ++insideSetter;HxString* r=setter(kind)(object,result,input);--insideSetter;return r;
}
static HxString* __fastcall textHook(void* a,HxString* b,HxString* c){return setText(0,a,b,c);}
static HxString* __fastcall htmlHook(void* a,HxString* b,HxString* c){return setText(1,a,b,c);}
static HxString* __fastcall starHook(void* a,HxString* b,HxString* c){return setText(2,a,b,c);}

// Root tracked text through Main's existing GC callbacks. The visit callback updates
// object pointers during compacting collections, so F8 never dereferences stale fields.
static void __fastcall markHook(void* object,void* context) {
    originalMark(object,context);
    static unsigned count=0;if(++count==1)log("GC rooting callback verified.");
    auto mark=(Marker)(base+0x8b62f0);uint32_t mask=*(uint32_t*)(base+0xbd6cd4);
    for(auto& e:fields)if(e->object && !(*(uint32_t*)((char*)e->object-4)&mask))mark(e->object,context);
}
static void __fastcall visitHook(void* object,void* context) {
    originalVisit(object,context);
    auto visit=(void(__fastcall*)(void*,void**))(*(void***)context)[1];
    for(auto& e:fields)if(e->object)visit(context,&e->object);
}
static void toggle() {
    if(insideSetter||replaying||GetCurrentThreadId()!=gameThread)return;
    chinese=!chinese;replaying=true;
    for(auto& e:fields) {
        HxString value=(chinese?e->translated:e->english)->value,result={};
        setter(e->kind)(e->object,&result,&value);
    }
    setHelpLanguage(chinese);
    replaying=false;WritePrivateProfileStringW(L"Language",L"Chinese",chinese?L"1":L"0",configPath);log(chinese?"F8: Chinese":"F8: English");
}
static decltype(&DispatchMessageW) dispatchW;
static decltype(&DispatchMessageA) dispatchA;
static bool handle(const MSG* m) {
    if(m && m->message==WM_KEYDOWN && m->wParam==VK_F8) {
        if(!(m->lParam&(1LL<<30)))toggle();
        return true;
    }
    return m&&m->message==WM_KEYUP&&m->wParam==VK_F8;
}
static LRESULT WINAPI dispatchWHook(const MSG* m){return handle(m)?0:dispatchW(m);}
static LRESULT WINAPI dispatchAHook(const MSG* m){return handle(m)?0:dispatchA(m);}
static bool hook(uintptr_t rva,void* replacement,void** original) {
    MH_STATUS s=MH_CreateHook((void*)(base+rva),replacement,original);if(s!=MH_OK){log(MH_StatusToString(s));return false;}return true;
}
static void patch(uintptr_t rva,const unsigned char* bytes,size_t n) {
    DWORD old;void* p=(void*)(base+rva);if(VirtualProtect(p,n,PAGE_EXECUTE_READWRITE,&old)){memcpy(p,bytes,n);VirtualProtect(p,n,old,&old);FlushInstructionCache(GetCurrentProcess(),p,n);}
}
static bool initialize() {
    base=(uintptr_t)GetModuleHandleW(nullptr);
    auto dos=(IMAGE_DOS_HEADER*)base;
    auto nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp!=TARGET_TIMESTAMP || nt->OptionalHeader.SizeOfImage!=TARGET_IMAGE_SIZE){log("Unsupported game build; no hooks installed.");return false;}
    GetModuleFileNameW(selfModule,configPath,MAX_PATH);wchar_t* slash=wcsrchr(configPath,L'\\');if(slash)wcscpy(slash+1,L"wilmzh.ini");
    chinese=GetPrivateProfileIntW(L"Language",L"Chinese",1,configPath)!=0;
    for (const auto& t : kTranslations) {
        exactTranslations[t.en] = t.zh;
        const std::string_view en(t.en), zh(t.zh);
        const auto enLimit = en.find(carryLimitToken), zhLimit = zh.find(carryLimitToken);
        if (enLimit != std::string_view::npos && zhLimit != std::string_view::npos) {
            carryLimitTranslations.push_back({en.substr(0, enLimit), en.substr(enLimit + carryLimitToken.size()),
                                              zh.substr(0, zhLimit), zh.substr(zhLimit + carryLimitToken.size())});
        } else if (en.size() >= 3 && en != " and ") {
            // A standalone " and " joins icons. Replacing it inside an unknown
            // sentence creates mixed-language text without translating the sentence.
            candidates[static_cast<unsigned char>(en[0])].push_back({&t, en.size()});
        }
    }
    if(memcmp((void*)(base+0x540d30),"\x40\x55\x41\x56\x41\x57",6) || memcmp((void*)(base+0x8ea30),"\x48\x89\x5c\x24\x08",5)){log("Unsupported game executable; no hooks installed.");return false;}
    if(MH_Initialize()!=MH_OK)return false;
#ifndef WILMZH_LEGACY_LOADER
    if(!prepareFontOverlay(selfModule)){log("Font overlay preparation failed. Check the original game assets and the Mod font recipe.");MH_Uninitialize();return false;}
#endif
    bool ok=hook(0x540d30,(void*)textHook,(void**)&originalText)&&hook(0x540750,(void*)htmlHook,(void**)&originalHtml)&&hook(0x8ea30,(void*)starHook,(void**)&originalStarText)&&hook(0x74a160,(void*)markHook,(void**)&originalMark);
    // Main's exact __Visit address is generated from the verified vtable.
    void** mainVtable=(void**)(base+MAIN_VTABLE_RVA);
    ok=ok&&MH_CreateHook(mainVtable[1],(void*)visitHook,(void**)&originalVisit)==MH_OK;
    ok=ok&&MH_CreateHook((void*)DispatchMessageW,(void*)dispatchWHook,(void**)&dispatchW)==MH_OK;
    ok=ok&&MH_CreateHook((void*)DispatchMessageA,(void*)dispatchAHook,(void**)&dispatchA)==MH_OK;
    if(ok)prepareHelpOverlay(selfModule,chinese,log);
    if(!ok||MH_EnableHook(MH_ALL_HOOKS)!=MH_OK){log("Hook initialization failed.");MH_DisableHook(MH_ALL_HOOKS);return false;}
    // Display whole tutorial paragraphs, avoiding split UTF-8 and partial translations.
    const unsigned char a[]={0x8b,0x03,0x89,0x86,0xb0,0x02,0,0,0xe9,0x93,0,0,0,0x90,0x90,0x90};
    const unsigned char b[]={0x8b,0x03,0x89,0x81,0xb0,0x02,0,0,0xe9,0x96,0,0,0,0x90,0x90,0x90};
    patch(0x6d3853,a,sizeof(a));patch(0x6d4d2d,b,sizeof(b));
    log("Wilmot Chinese mod loaded. F8 toggles language in game.");return true;
}
extern "C" BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){selfModule=instance;DisableThreadLibraryCalls(instance);initialize();}return TRUE;
}
