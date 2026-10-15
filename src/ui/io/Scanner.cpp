#include "ui/io/Scanner.h"

#include <QDir>
#include <QObject>
#include <QWidget>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <wia.h>
#endif

namespace {

std::function<QStringList(QString*)>& standIn()
{
    static std::function<QStringList(QString*)> it;
    return it;
}

}

void Scanner::setStandIn(std::function<QStringList(QString*)> it)
{
    standIn() = std::move(it);
}

QStringList Scanner::acquire(QWidget* parent, QString const& folder, QString* error)
{
    if (error)
        error->clear();
    if (standIn())
        return standIn()(error);
    QDir().mkpath(folder);
#ifdef Q_OS_WIN
    // The system's scanning dialog: the scanner, the kind of picture and the area are chosen there, and the scan made.
    HRESULT const started = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool const ours = SUCCEEDED(started);
    QStringList files;
    IWiaDevMgr2* manager = nullptr;
    HRESULT result = CoCreateInstance(CLSID_WiaDevMgr2, nullptr, CLSCTX_LOCAL_SERVER, IID_IWiaDevMgr2, reinterpret_cast<void**>(&manager));
    if (FAILED(result) || !manager) {
        if (error)
            *error = QObject::tr("This computer has no scanning service (Windows Image Acquisition).");
    } else {
        BSTR const where = SysAllocString(reinterpret_cast<wchar_t const*>(QDir::toNativeSeparators(folder).utf16()));
        BSTR const name = SysAllocString(L"Scan");
        LONG count = 0;
        BSTR* paths = nullptr;
        IWiaItem2* item = nullptr;
        result = manager->GetImageDlg(0, nullptr, parent ? reinterpret_cast<HWND>(parent->window()->winId()) : nullptr, where, name, &count, &paths,
                                      &item);
        if (result == WIA_S_NO_DEVICE_AVAILABLE) {
            if (error)
                *error = QObject::tr("No scanner was found. Is it plugged in and switched on?");
        } else if (FAILED(result)) {
            if (error)
                *error = QObject::tr("The scan did not work (error %1).").arg(QString::number(quint32(result), 16));
        }
        for (LONG k = 0; paths && k < count; ++k) {
            files << QDir::fromNativeSeparators(QString::fromWCharArray(paths[k]));
            SysFreeString(paths[k]);
        }
        if (paths)
            CoTaskMemFree(paths);
        if (item)
            item->Release();
        SysFreeString(name);
        SysFreeString(where);
        manager->Release();
    }
    if (ours)
        CoUninitialize();
    return files;
#else
    (void)parent;
    if (error)
        *error = QObject::tr("Scanning is not ready on this system yet: scan to a file and use Import > Images.");
    return {};
#endif
}
