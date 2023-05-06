#define STRICT
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// パス操作
#include <pathcch.h>
#pragma	comment(lib,"Pathcch.lib")

// コモンコントロール
#include <commctrl.h>
#pragma	comment(lib,"Comctl32.lib")

// シェルAPI
#include <shlwapi.h>
#include <shellapi.h>
#pragma	comment(lib,"Shlwapi.lib")
#pragma	comment(lib,"Shell32.lib")

// GDI+
#include <ole2.h>
#include <gdiplus.h>
#pragma	comment(lib,"Gdiplus.lib")

// C++標準ライブラリ
#include <memory>
#include <string>
#include <list>
#include <set>
#include <format>

namespace
{
    LPCWSTR APP_NAME = L"ImageFlicker";
    const UINT BUF_SIZE = 4096;

    // UIハンドル
    HINSTANCE hInstance = NULL;
    HWND hWindow = NULL;
    HWND hStatusbar = NULL;

    // 画像キュー
    std::list<std::wstring> imageList;
    std::list<std::wstring>::iterator imageCurrent;

    // 画像ファイルとして認識する拡張子
    std::set<std::wstring> imageExt = {L".png", L".jpg", L".jpeg"};
}

// アクティブなファイル名
std::wstring CurrentFile()
{
    if (imageList.empty()) return std::wstring();
    if (imageCurrent == imageList.end()) imageCurrent = imageList.begin();
    return (imageCurrent != imageList.end()) ? *imageCurrent : std::wstring();
}

// ページ表記
std::wstring CurrentPage()
{
    if (imageList.empty()) return std::wstring();
    size_t count = imageList.size();
    size_t index = std::distance(imageList.begin(), imageCurrent) + 1;
    return std::format(L"{0} / {1}", index, count);
}

// キューに追加
void AddToImageList(std::wstring path)
{
    DWORD attr = GetFileAttributes(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return;
    if (attr & FILE_ATTRIBUTE_READONLY) return;
    if (attr & FILE_ATTRIBUTE_HIDDEN) return;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        WCHAR szBuff[BUF_SIZE];
        PathCchCombine(szBuff, BUF_SIZE, path.c_str(), L"*.*");
        WIN32_FIND_DATA fd;
        HANDLE hFile = FindFirstFile(szBuff, &fd);
        if (INVALID_HANDLE_VALUE == hFile) return;
        do {
            if ( fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) continue;
            PathCchCombine(szBuff, BUF_SIZE, path.c_str(), fd.cFileName);
            AddToImageList(szBuff);
        } while( FindNextFile(hFile, &fd) );
        FindClose( hFile );
    } else {
        auto ext = PathFindExtension(path.c_str());
        if (imageExt.contains(ext)) {
            imageList.push_back(path);
        }
    }
}

// 情報更新
void UpdateStatus()
{
    SendMessage(hStatusbar, SB_SETTEXT, 0, (LPARAM)CurrentFile().c_str());
    SendMessage(hStatusbar, SB_SETTEXT, 1, (LPARAM)CurrentPage().c_str());
}

// 表示更新
void UpdateView()
{
    UpdateStatus();
    InvalidateRect(hWindow, NULL, FALSE);
}

// ファイルの移動
void MoveImage(UINT number)
{
    auto path = CurrentFile();
    if (path.empty()) return;

    // パスの分解
    WCHAR szBuff[BUF_SIZE];
    PWCHAR pPart;
    if (!GetFullPathName(path.c_str(), BUF_SIZE, szBuff, &pPart)) return;
    std::wstring fileSpec = pPart;

    PathCchRemoveFileSpec(szBuff, BUF_SIZE);
    std::wstring fileDir = szBuff;

    // 新しいパス（無ければ作る）
    std::wstring newDir = std::format(L"{0}\\{1}", szBuff, number);
    std::wstring newPath = std::format(L"{0}\\{1}", newDir, fileSpec);
    if (!PathFileExists(newDir.c_str())) {
        CreateDirectory(newDir.c_str(), NULL);
    }

    // 移動
    MoveFile(path.c_str(), newPath.c_str());

    // キューから除去
    imageCurrent = imageList.erase(imageCurrent);
    UpdateView();
}

// 画像描画
void DrawImage(HDC hdc)
{
    using namespace Gdiplus;

    Graphics graphics(hdc);
    graphics.Clear(Color::Gray);

    auto path = CurrentFile();
    if (path.empty()) return;

    //画像サイズ
    Image image(path.c_str());
    UINT imageWidth = image.GetWidth();
    UINT imageHeight = image.GetHeight();

    //クライアント領域サイズ
    RECT rect;
    GetClientRect(hWindow, &rect);
    UINT viewWidth = rect.right - rect.left;
    UINT viewHeight = rect.bottom - rect.top;

    // アスペクト比をキープして表示領域に納める計算
    double imageRate = static_cast<double>(imageWidth) / static_cast<double>(imageHeight);
    double viewRate = static_cast<double>(viewWidth) / static_cast<double>(viewHeight);
    if (imageRate < viewRate) {
        viewWidth = static_cast<UINT>(viewHeight * imageRate);
    } else {
        viewHeight = static_cast<UINT>(viewWidth / imageRate);
    }

    // 描画
    graphics.DrawImage(&image, 0, 0, viewWidth, viewHeight);
}

// 生成
void OnCreate(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    hWindow = hWnd;

    // コモンコントロールの初期化
    INITCOMMONCONTROLSEX ic = {
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_BAR_CLASSES,
    };
    InitCommonControlsEx(&ic);

    // ステータスバー
    hStatusbar = CreateWindowEx(
        0, STATUSCLASSNAME, NULL,
        WS_CHILD | WS_VISIBLE | CCS_BOTTOM | SBARS_SIZEGRIP,
        0, 0, 0, 0,
        hWnd, NULL, hInstance, NULL);
}

// サイズ変更
void OnSize(int cx, int cy)
{
    int nParts[] = { cx - 100, -1 };
    SendMessage(hStatusbar, SB_SETPARTS, 2, (LPARAM)&nParts);
    SendMessage(hStatusbar, WM_SIZE, 0, 0);
    UpdateStatus();
}

// 描画
void OnPaint()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hWindow, &ps);
    DrawImage(hdc);
    EndPaint(hWindow, &ps);
}

// ファイルのドロップ
void OnDropFiles(HDROP hDrop)
{
    WCHAR szBuff[BUF_SIZE];
    UINT nSize = DragQueryFile(hDrop, -1, NULL, 0);
    bool queueWasEmpty = imageList.empty();
    for (UINT i = 0; i < nSize; i++) {
        UINT ret = DragQueryFile(hDrop, i, szBuff, BUF_SIZE);
        if (ret == -1) continue;
        AddToImageList(szBuff);
    }
    DragFinish(hDrop);

    // ブランク状態からだったらカレントを先頭に
    if (queueWasEmpty && !imageList.empty()) {
        imageCurrent = imageList.begin();
        UpdateView();
    }
}

// キー押下
void OnKeyDown(UINT keyCode, bool repeat)
{
    if (imageList.empty()) return;
    switch (keyCode)
    {
    case VK_RIGHT: // ページ送り
        if (imageCurrent != imageList.end())
        {
            imageCurrent++;
            if (imageCurrent == imageList.end())
            {
                imageCurrent--;
                return;
            }
            UpdateView();
        }
        return;
    case VK_LEFT: // ページ戻し
        if (imageCurrent != imageList.begin())
        {
            imageCurrent--;
            UpdateView();
        }
        return;
    }

    // テンキー（ファイル移動）
    if (VK_NUMPAD0 <= keyCode && keyCode <= VK_NUMPAD9)
    {
        UINT number = keyCode - VK_NUMPAD0;
        MoveImage(number);
    }
}

// Windowsプロセス
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        OnCreate(hWnd, msg, wParam, lParam);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_SIZE:
        OnSize(LOWORD(lParam), HIWORD(lParam));
        break;
    case WM_PAINT:
        OnPaint();
        break;
    case WM_KEYDOWN:
        OnKeyDown(static_cast<UINT>(wParam), (lParam & 0x40000000));
        break;
    case WM_DROPFILES:
        OnDropFiles(reinterpret_cast<HDROP>(wParam));
        break;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
        break;
    }
    return 0;
}

// Windowsメイン
int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_  HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
    hInstance = hInst;
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    LPCWSTR className = APP_NAME;
    WNDCLASSEX wc = {
        .cbSize = sizeof(WNDCLASSEX),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = WndProc,
        .cbClsExtra = 0,
        .cbWndExtra = 0,
        .hInstance = hInstance,
        .hIcon = LoadIcon(hInstance, IDI_APPLICATION),
        .hCursor = LoadCursor(NULL, IDC_ARROW),
        .hbrBackground = (HBRUSH)(COLOR_WINDOW + 1),
        .lpszMenuName = NULL,
        .lpszClassName = className,
        .hIconSm = LoadIcon(hInstance, IDI_APPLICATION),
    };
    if (!RegisterClassEx(&wc)) return 1;

    HWND hWnd = CreateWindowEx(
        WS_EX_OVERLAPPEDWINDOW,
        className,
        APP_NAME,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        768, 512+20,
        NULL,
        NULL,
        hInstance,
        NULL
    );
    if (!hWnd) return 1;

    // ファイルのドロップを受ける
    DragAcceptFiles(hWnd, TRUE);

    // GDI+のスタートアップ
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // メインループ
    ShowWindow(hWnd, nShowCmd);
    UpdateWindow(hWnd);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // GDI+のシャットダウン
    Gdiplus::GdiplusShutdown(gdiplusToken);

    return (int)msg.wParam;
}