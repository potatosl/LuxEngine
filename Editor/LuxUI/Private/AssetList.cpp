#include <LuxUI/AssetList.hpp>
#include <QDrag>
#include <QPointer>

namespace LuxUI {
AssetList::AssetList(Widget* parent) : ListWidget(parent) {
    setDragEnabled(true);
}

void AssetList::startDrag(Flags::DropActions) {
    const auto* item = currentItem();
    if (!item)
        return;
    const auto path = item->data(Flags::UserRole).toString();
    const FileInfo file(path);
    if (path.isEmpty() || !file.isAbsolute() || !file.isFile())
        return;
    auto* mime = new MimeData;
    mime->setUrls({Url::fromLocalFile(path)});
    QPointer<QDrag> drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Flags::CopyAction);
    if (drag)
        drag->deleteLater();
}
} // namespace LuxUI
