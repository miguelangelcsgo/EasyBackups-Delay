// ─────────────────────────────────────────────────────────────────────────────
// ffmpeg-delayload.cpp
//
// Hace que el plugin use SU PROPIA copia de FFmpeg (avcodec/avutil/swscale),
// empaquetada al lado del .dll en la subcarpeta "EasyBackupandDelay-ffmpeg\", en
// vez de depender de la versión que traiga el OBS del usuario.
//
// Cómo: esas DLLs se linkean con DELAY-LOAD (ver CMakeLists), así que su carga se
// difiere hasta la primera llamada. Acá enganchamos el notify-hook de delay-load y,
// cuando el loader va a cargar una DLL de FFmpeg, la cargamos NOSOTROS por ruta
// absoluta desde nuestra subcarpeta (con LOAD_WITH_ALTERED_SEARCH_PATH, así sus
// dependencias también salen de ahí). Si nuestra copia no está, devolvemos NULL y el
// loader usa la búsqueda normal (las DLLs que trae OBS) — nunca empeora el caso.
//
// Nota sobre nombres: Windows indexa las DLLs cargadas por su NOMBRE base. Si OBS ya
// cargó una DLL con el mismo nombre (misma versión mayor → ABI compatible), el loader
// devuelve esa y todo funciona igual. El bundling gana justo cuando OBS trae OTRA
// versión (otro nombre, p. ej. avcodec-62 vs avcodec-61): ahí no hay choque y se usa
// la nuestra.
// ─────────────────────────────────────────────────────────────────────────────

#if defined(_MSC_VER)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <delayimp.h>

#include <string>

// ¿El nombre de DLL es una de las libs de FFmpeg que empaquetamos? Se compara por
// prefijo para no atarse a la versión (avcodec-62, avcodec-61, …).
static bool is_ffmpeg_dll(const char* dll)
{
    return dll && (
        _strnicmp(dll, "avcodec-", 8) == 0 ||
        _strnicmp(dll, "avutil-",  7) == 0 ||
        _strnicmp(dll, "swscale-", 8) == 0);
}

// Carpeta de ESTE módulo (la DLL del plugin). La dirección de una función propia
// identifica el módulo sin depender de su nombre de archivo.
static std::wstring self_dir()
{
    HMODULE hm = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&is_ffmpeg_dll), &hm);
    wchar_t buf[MAX_PATH * 2] = {};
    GetModuleFileNameW(hm, buf, _countof(buf));
    std::wstring p = buf;
    const size_t slash = p.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? std::wstring() : p.substr(0, slash);
}

static FARPROC WINAPI eobd_delayload_hook(unsigned event, PDelayLoadInfo info)
{
    if (event == dliNotePreLoadLibrary && info && is_ffmpeg_dll(info->szDll)) {
        int n = MultiByteToWideChar(CP_ACP, 0, info->szDll, -1, nullptr, 0);
        if (n > 0) {
            std::wstring wname(static_cast<size_t>(n - 1), L'\0');
            MultiByteToWideChar(CP_ACP, 0, info->szDll, -1, wname.data(), n);
            const std::wstring full =
                self_dir() + L"\\EasyBackupandDelay-ffmpeg\\" + wname;
            // NULL si no está nuestra copia → el loader hace la búsqueda por defecto.
            HMODULE h = LoadLibraryExW(full.c_str(), nullptr,
                                       LOAD_WITH_ALTERED_SEARCH_PATH);
            return reinterpret_cast<FARPROC>(h);
        }
    }
    return nullptr;
}

// delayimp.h declara este puntero global; definirlo instala nuestro hook.
extern "C" const PfnDliHook __pfnDliNotifyHook2 = eobd_delayload_hook;

#endif // _MSC_VER
