#include "App.h"
#include <commdlg.h>
#include <process.h>
#include <strsafe.h>
#include "KeyLayout.h"

static_assert(sizeof(MK_PROFILE) == 432, "Profile ABI changed");
static_assert(sizeof(MK_STATE) == 448, "State ABI changed");
static HWND Window, StatusLabel, SelectionLabel, IntensityEdit;
static HWND KeyButtons[MK_KEY_COUNT], Actions[8];
static HFONT Font, KeyFont;
static MK_PROFILE Profile;
static bool Selected[MK_KEY_COUNT], Busy, Dirty;
static UINT Dpi = 96;
static COLORREF CustomColors[16];
static constexpr UINT ResultMessage = WM_APP + 1;
static constexpr int KeyBase = 1000;
enum { SelectAll = 100, SelectNone, PickColor, SetIntensity, Save, WindowsMode, Reload, Refresh };

struct Job {
    HWND Owner;
    ULONG Operation;
    MK_PROFILE Profile;
    bool ReloadProfile;
};
struct JobResult {
    DWORD Error;
    PCWSTR Stage;
    MK_STATE State;
    ULONG Operation;
    bool ReloadProfile;
};

static int Scale(int Value) { return MulDiv(Value, static_cast<int>(Dpi), 96); }

static void ShowSelection()
{
    unsigned count = 0;
    for (bool selected : Selected) if (selected) ++count;
    WCHAR text[128];
    StringCchPrintfW(text, ARRAYSIZE(text), L"%u keys selected. Click a key; Ctrl+click adds or removes keys.", count);
    SetWindowTextW(SelectionLabel, text);
    for (HWND button : KeyButtons) InvalidateRect(button, nullptr, FALSE);
}

static void SetBusy(bool Value)
{
    Busy = Value;
    for (HWND button : Actions) EnableWindow(button, !Value);
    for (HWND button : KeyButtons) EnableWindow(button, !Value);
    EnableWindow(IntensityEdit, !Value);
}

static unsigned __stdcall RequestThread(void* Parameter)
{
    Job* job = static_cast<Job*>(Parameter);
    JobResult* result = new JobResult{};
    result->Operation = job->Operation;
    result->ReloadProfile = job->ReloadProfile;
    result->Error = ProfileRequest(job->Operation, &job->Profile, &result->State, &result->Stage);
    if (!PostMessageW(job->Owner, ResultMessage, 0, reinterpret_cast<LPARAM>(result))) delete result;
    delete job;
    return 0;
}

static void Request(ULONG Operation, bool ReloadProfile = false)
{
    if (Busy) return;
    SetBusy(true);
    SetWindowTextW(StatusLabel, L"Contacting the profile helper...");
    Job* job = new Job{ Window, Operation, Profile, ReloadProfile };
    uintptr_t thread = _beginthreadex(nullptr, 0, RequestThread, job, 0, nullptr);
    if (!thread) {
        delete job;
        SetBusy(false);
        SetWindowTextW(StatusLabel, L"Could not start the request. Try again.");
    } else {
        CloseHandle(reinterpret_cast<HANDLE>(thread));
    }
}

static void ReceiveResult(JobResult* Result)
{
    SetBusy(false);
    WCHAR text[700];
    if (Result->Error) {
        WCHAR detail[400] = {};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, Result->Error, 0, detail, ARRAYSIZE(detail), nullptr);
        StringCchPrintfW(text, ARRAYSIZE(text),
            L"%s failed (error %lu): %s\nYour edits have not been applied. Retry or report this error and step.",
            Result->Stage, Result->Error, detail);
    } else {
        if (Result->ReloadProfile || Result->Operation == MK_OP_SET) {
            Profile = Result->State.Profile;
            Dirty = false;
        }
        const bool applied = Result->State.AppliedGeneration == Result->State.Generation;
        StringCchPrintfW(text, ARRAYSIZE(text),
            L"%s  |  %s  |  Last hardware status: 0x%08lX\nThe helper stops after 15 seconds without requests. Your saved profile stays in the driver.",
            Result->State.Profile.Mode == MK_MODE_STATIC ? L"Saved static profile has control" : L"Windows Dynamic Lighting has control",
            applied ? L"Latest driver request completed" : L"Hardware update pending; use Refresh status",
            static_cast<ULONG>(Result->State.LastTransportStatus));
        ShowSelection();
    }
    SetWindowTextW(StatusLabel, text);
    delete Result;
}

static HWND Control(PCWSTR Class, PCWSTR Text, DWORD Style, int Id,
                    int X, int Y, int Width, int Height)
{
    HWND control = CreateWindowExW(0, Class, Text, WS_CHILD | WS_VISIBLE | Style,
        Scale(X), Scale(Y), Scale(Width), Scale(Height), Window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(Id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(Font), TRUE);
    return control;
}

static void CreateControls()
{
    Font = CreateFontW(-Scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    KeyFont = CreateFontW(-Scale(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    Control(L"STATIC", L"MK4400 Pro · Static RGB", 0, 0, 16, 14, 850, 24);
    Control(L"STATIC", L"Choose keys, assign colors, then Save & apply. No animation or effect engine.", 0, 0, 16, 40, 880, 24);
    for (int i = 0; i < MK_KEY_COUNT; ++i) {
        const KeyRect& key = Keys[i];
        KeyButtons[i] = Control(L"BUTTON", key.Name, WS_TABSTOP | BS_OWNERDRAW,
            KeyBase + i, key.X + 16, key.Y + 76, key.W, key.H);
    }
    SelectionLabel = Control(L"STATIC", L"", 0, 0, 16, 310, 900, 22);
    Actions[0] = Control(L"BUTTON", L"Select all", WS_TABSTOP, SelectAll, 16, 338, 90, 30);
    Actions[1] = Control(L"BUTTON", L"Clear selection", WS_TABSTOP, SelectNone, 114, 338, 125, 30);
    Actions[2] = Control(L"BUTTON", L"Choose color...", WS_TABSTOP, PickColor, 247, 338, 130, 30);
    Control(L"STATIC", L"Brightness (0–255)", 0, 0, 393, 344, 140, 23);
    IntensityEdit = Control(L"EDIT", L"255", WS_BORDER | WS_TABSTOP | ES_NUMBER,
        200, 535, 338, 55, 30);
    SendMessageW(IntensityEdit, EM_SETLIMITTEXT, 3, 0);
    Actions[3] = Control(L"BUTTON", L"Set brightness", WS_TABSTOP, SetIntensity, 600, 338, 130, 30);
    Actions[4] = Control(L"BUTTON", L"Save & apply static profile", WS_TABSTOP, Save, 16, 380, 215, 32);
    Actions[5] = Control(L"BUTTON", L"Use Windows lighting", WS_TABSTOP, WindowsMode, 241, 380, 190, 32);
    Actions[6] = Control(L"BUTTON", L"Reload saved colors", WS_TABSTOP, Reload, 441, 380, 175, 32);
    Actions[7] = Control(L"BUTTON", L"Refresh status", WS_TABSTOP, Refresh, 626, 380, 130, 32);
    StatusLabel = Control(L"STATIC", L"", 0, 0, 16, 426, 890, 66);
    ShowSelection();
}

static void DrawKey(const DRAWITEMSTRUCT* Draw)
{
    int index = static_cast<int>(Draw->CtlID) - KeyBase;
    if (index < 0 || index >= MK_KEY_COUNT) return;
    MK_COLOR color = Profile.Colors[index];
    int r = color.R * color.Intensity / 255;
    int g = color.G * color.Intensity / 255;
    int b = color.B * color.Intensity / 255;
    HBRUSH brush = CreateSolidBrush(RGB(r, g, b));
    FillRect(Draw->hDC, &Draw->rcItem, brush);
    DeleteObject(brush);
    HBRUSH border = CreateSolidBrush(Selected[index] ? RGB(0, 120, 215) : RGB(90, 90, 90));
    RECT rect = Draw->rcItem;
    FrameRect(Draw->hDC, &rect, border);
    if (Selected[index]) {
        InflateRect(&rect, -Scale(2), -Scale(2));
        FrameRect(Draw->hDC, &rect, border);
    }
    DeleteObject(border);
    SetBkMode(Draw->hDC, TRANSPARENT);
    SetTextColor(Draw->hDC, (r * 299 + g * 587 + b * 114) > 145000 ? RGB(0, 0, 0) : RGB(255, 255, 255));
    HGDIOBJ old = SelectObject(Draw->hDC, KeyFont);
    DrawTextW(Draw->hDC, Keys[index].Name, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(Draw->hDC, old);
    if (Draw->itemState & ODS_FOCUS) DrawFocusRect(Draw->hDC, &rect);
}

static bool HasSelection()
{
    for (bool value : Selected) if (value) return true;
    SetWindowTextW(StatusLabel, L"Select at least one key first.");
    return false;
}

static LRESULT CALLBACK WindowProcedure(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam)
{
    switch (Message) {
    case WM_CREATE:
        Window = Hwnd;
        Dpi = GetDpiForWindow(Hwnd);
        CreateControls();
        Request(MK_OP_GET, true);
        return 0;
    case WM_DRAWITEM:
        DrawKey(reinterpret_cast<DRAWITEMSTRUCT*>(LParam));
        return TRUE;
    case ResultMessage:
        ReceiveResult(reinterpret_cast<JobResult*>(LParam));
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(WParam);
        if (Busy) return 0;
        if (id >= KeyBase && id < KeyBase + MK_KEY_COUNT) {
            int index = id - KeyBase;
            if (!(GetKeyState(VK_CONTROL) & 0x8000)) {
                for (bool& selected : Selected) selected = false;
                Selected[index] = true;
            } else Selected[index] = !Selected[index];
            ShowSelection();
            return 0;
        }
        switch (id) {
        case SelectAll:
        case SelectNone:
            for (bool& selected : Selected) selected = id == SelectAll;
            ShowSelection();
            break;
        case PickColor: {
            if (!HasSelection()) break;
            CHOOSECOLORW choose = { sizeof(choose) };
            choose.hwndOwner = Hwnd;
            choose.lpCustColors = CustomColors;
            choose.Flags = CC_FULLOPEN | CC_RGBINIT;
            for (int i = 0; i < MK_KEY_COUNT; ++i) if (Selected[i]) {
                choose.rgbResult = RGB(Profile.Colors[i].R, Profile.Colors[i].G, Profile.Colors[i].B);
                break;
            }
            if (ChooseColorW(&choose)) {
                for (int i = 0; i < MK_KEY_COUNT; ++i) if (Selected[i]) {
                    Profile.Colors[i].R = GetRValue(choose.rgbResult);
                    Profile.Colors[i].G = GetGValue(choose.rgbResult);
                    Profile.Colors[i].B = GetBValue(choose.rgbResult);
                }
                Dirty = true;
                ShowSelection();
                SetWindowTextW(StatusLabel, L"Colors edited. Save & apply to send this static profile to the keyboard.");
            }
            break;
        }
        case SetIntensity: {
            if (!HasSelection()) break;
            BOOL valid = FALSE;
            UINT intensity = GetDlgItemInt(Hwnd, 200, &valid, FALSE);
            if (!valid || intensity > 255) { SetWindowTextW(StatusLabel, L"Enter brightness from 0 through 255."); break; }
            for (int i = 0; i < MK_KEY_COUNT; ++i) if (Selected[i])
                Profile.Colors[i].Intensity = static_cast<UCHAR>(intensity);
            Dirty = true;
            ShowSelection();
            SetWindowTextW(StatusLabel, L"Brightness edited. Save & apply when ready.");
            break;
        }
        case Save:
            Profile.Mode = MK_MODE_STATIC;
            Request(MK_OP_SET);
            break;
        case WindowsMode:
            Profile.Mode = MK_MODE_WINDOWS;
            Request(MK_OP_SET);
            break;
        case Reload:
            if (!Dirty || MessageBoxW(Hwnd, L"Discard unsaved edits and reload the saved colors?",
                L"Reload profile", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) Request(MK_OP_GET, true);
            break;
        case Refresh:
            Request(MK_OP_GET);
            break;
        }
        return 0;
    }
    case WM_CLOSE:
        if (Busy) { SetWindowTextW(StatusLabel, L"Please wait for the current request to finish before closing."); return 0; }
        if (Dirty && MessageBoxW(Hwnd, L"Close without saving these color changes?", L"Unsaved colors",
            MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return 0;
        DestroyWindow(Hwnd);
        return 0;
    case WM_DESTROY:
        DeleteObject(Font);
        DeleteObject(KeyFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(Hwnd, Message, WParam, LParam);
}

int RunEditor(HINSTANCE Instance, int Show)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    Dpi = GetDpiForSystem();
    Profile.Version = MK_API_VERSION;
    Profile.Count = MK_KEY_COUNT;
    for (int i = 0; i < MK_KEY_COUNT; ++i) {
        Profile.Colors[i] = { 255, 255, 255, 255 };
        Selected[i] = true;
    }
    WNDCLASSW cls = {};
    cls.lpfnWndProc = WindowProcedure;
    cls.hInstance = Instance;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    cls.lpszClassName = L"Mk4400StaticEditor";
    if (!RegisterClassW(&cls)) return static_cast<int>(GetLastError());
    RECT bounds = { 0, 0, Scale(930), Scale(500) };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&bounds, style, FALSE, 0, Dpi);
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"MK4400 Static RGB", style,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, Instance, nullptr);
    if (!window) return static_cast<int>(GetLastError());
    ShowWindow(window, Show);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE Instance, HINSTANCE, PWSTR CommandLine, int Show)
{
    if (wcscmp(CommandLine, L"--service") == 0) return static_cast<int>(RunProfileService());
    return RunEditor(Instance, Show);
}
