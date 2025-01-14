#include "pch.h"

#include "resource.h"

#define PLUGIN_VERSION 1
#define PLUGIN_VERSION_STR "1.0"

#ifndef DLL_EXPORT
#define DLL_EXPORT __declspec(dllexport)
#endif

namespace {

constexpr size_t kCollapsedSymbolDesiredLength = 80;

enum {
    MENU_ABOUT = 1,
};

HINSTANCE g_hDllInst;
int g_pluginHandle;

class SymbolInfoWrapper {
   public:
    ~SymbolInfoWrapper() {
        if (info.freeDecorated) {
            BridgeFree(info.decoratedSymbol);
        }
        if (info.freeUndecorated) {
            BridgeFree(info.undecoratedSymbol);
        }
    }

    SYMBOLINFO* operator&() { return &info; }
    SYMBOLINFO* operator->() { return &info; }

   private:
    SYMBOLINFO info;
};

std::string CollapseLabelTemplates(std::string_view label) {
    static const boost::regex collapsePattern(R"(<(<\.\.\.>|[^<>])*>)");
    std::string result;
    boost::regex_replace(std::back_inserter(result), label.begin(), label.end(),
                         collapsePattern, "<...>");

    return result != label ? result : std::string();
}

std::string CollapseLabelNamespaces(std::string_view label) {
    static const boost::regex collapsePattern(
        R"(([a-zA-Z_](<\.\.\.>|[a-zA-Z0-9_])*::)+)");
    std::string result;
    boost::regex_replace(std::back_inserter(result), label.begin(), label.end(),
                         collapsePattern, "[...]::");

    return result != label ? result : std::string();
}

void AddInfoLineForCollapsedLabel(std::string label) {
    if (label.size() <= kCollapsedSymbolDesiredLength) {
        return;
    }

    GuiAddInfoLine("-- collapsed symbol --");

    while (true) {
        std::string labelCollapsed = CollapseLabelTemplates(label);
        if (labelCollapsed.empty()) {
            break;
        }

        label = std::move(labelCollapsed);
        if (label.size() <= kCollapsedSymbolDesiredLength) {
            GuiAddInfoLine(label.c_str());
            return;
        }
    }

    if (std::string labelCollapsed = CollapseLabelNamespaces(label);
        !labelCollapsed.empty()) {
        label = std::move(labelCollapsed);
        if (label.size() <= kCollapsedSymbolDesiredLength) {
            GuiAddInfoLine(label.c_str());
            return;
        }
    }

    GuiAddInfoLine(label.c_str());
}

void AddInfoLinesForWordWrappedLabel(std::string_view label) {
    GuiAddInfoLine("-- formatted symbol --");

    // Matches delimiters or consecutive non-delimiters.
    // https://stackoverflow.com/a/27706957
    // Exception examples: fn(unsigned int *), T<1337>.
    static const boost::regex delimiters(
        R"((\([a-zA-Z0-9_*& ]+\)|<[a-zA-Z0-9_*& ]+>|[,<>()]|[^,<>()]+))");

    boost::regex_iterator<std::string_view::iterator> rit(
        label.begin(), label.end(), delimiters);
    boost::regex_iterator<std::string_view::iterator> rend;

    std::string line;
    bool lineReady = false;
    size_t indentLevel = 0;

    static const boost::regex qualifiers(R"( *((const|volatile|[*&]) *)+)");
    static const boost::regex methodModifiers(R"( *const *)");

    for (; rit != rend; ++rit) {
        auto s = rit->str();

        if (lineReady) {
            if (s == ")" || s == ">") {
                line += s;
                if (indentLevel > 0) {
                    indentLevel--;
                }
                continue;
            }

            // The qualifiers check is to keep "x<y> const &" together. The
            // methodModifiers check is to keep "method(...) const" together.
            if (s == "," ||
                (line.back() == '>' && boost::regex_match(s, qualifiers)) ||
                (line.back() == ')' &&
                 boost::regex_match(s, methodModifiers))) {
                line += s;
                continue;
            }

            GuiAddInfoLine(line.c_str());
            line.clear();
            lineReady = false;
        }

        if (line.empty()) {
            auto firstNonSpace = s.find_first_not_of(' ');
            if (firstNonSpace == s.npos) {
                continue;
            }

            if (indentLevel > 0) {
                line.assign(indentLevel * 2, ' ');
            }

            line += s.substr(firstNonSpace);
        } else {
            line += s;
        }

        if (s == ",") {
            lineReady = true;
            continue;
        }

        if (s == ")" || s == ">") {
            lineReady = true;
            if (indentLevel > 0) {
                indentLevel--;
            }
            continue;
        }

        if (s == "(" || s == "<") {
            lineReady = true;
            indentLevel++;
            continue;
        }
    }

    if (!line.empty()) {
        GuiAddInfoLine(line.c_str());
    }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD ul_reason_for_call,
                      LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            g_hDllInst = hModule;
            break;

        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }

    return TRUE;
}

extern "C" DLL_EXPORT bool pluginit(PLUG_INITSTRUCT* initStruct) {
    initStruct->pluginVersion = PLUGIN_VERSION;
    initStruct->sdkVersion = PLUG_SDKVERSION;
    strcpy_s(initStruct->pluginName, "Symbol tl;dr");
    g_pluginHandle = initStruct->pluginHandle;

    _plugin_logputs("Symbol tl;dr v" PLUGIN_VERSION_STR);
    _plugin_logputs("  By m417z");

    return true;
}

extern "C" DLL_EXPORT void plugsetup(PLUG_SETUPSTRUCT* setupStruct) {
    int hMenu = setupStruct->hMenu;

    HRSRC hResource =
        FindResource(g_hDllInst, MAKEINTRESOURCE(IDB_ICON), L"PNG");
    if (hResource) {
        HGLOBAL hMemory = LoadResource(g_hDllInst, hResource);
        if (hMemory) {
            DWORD dwSize = SizeofResource(g_hDllInst, hResource);
            LPVOID lpAddress = LockResource(hMemory);
            if (lpAddress) {
                ICONDATA IconData;
                IconData.data = lpAddress;
                IconData.size = dwSize;

                _plugin_menuseticon(hMenu, &IconData);
            }
        }
    }

    _plugin_menuaddentry(hMenu, MENU_ABOUT, "&About");
}

extern "C" DLL_EXPORT void CBSELCHANGED(CBTYPE, PLUG_CB_SELCHANGED* selInfo) {
    SymbolInfoWrapper info;
    if (!DbgGetSymbolInfoAt(selInfo->VA, &info) || !*info->undecoratedSymbol) {
        duint start;
        if (!DbgFunctionGet(selInfo->VA, &start, nullptr) ||
            !DbgGetSymbolInfoAt(start, &info) || !*info->undecoratedSymbol) {
            return;
        }
    }

    std::string label = info->undecoratedSymbol;
    AddInfoLineForCollapsedLabel(label);
    AddInfoLinesForWordWrappedLabel(label);
}

extern "C" DLL_EXPORT void CBMENUENTRY(CBTYPE, PLUG_CB_MENUENTRY* info) {
    switch (info->hEntry) {
        case MENU_ABOUT:
            MessageBox(
                GetActiveWindow(),
                L"Symbol tl;dr v" TEXT(PLUGIN_VERSION_STR) L"\n" 
                L"By m417z\n" 
                L"https://github.com/m417z/x64dbg-symbol-tldr",
                L"About", MB_ICONINFORMATION);
            break;
    }
}
