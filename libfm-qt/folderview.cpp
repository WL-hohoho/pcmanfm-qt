/*
    <one line to give the library's name and an idea of what it does.>
    Copyright (C) 2012  Hong Jen Yee (PCMan) <pcman.tw@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/


#include "folderview.h"
#include "foldermodel.h"
#include <QHeaderView>
#include <QVBoxLayout>
#include <QContextMenuEvent>
#include "proxyfoldermodel.h"
#include "folderitemdelegate.h"
#include "dndactionmenu.h"
#include "filemenu.h"
#include "foldermenu.h"
#include "filelauncher.h"
#include <QTimer>
#include <QDate>
#include <QDebug>
#include <QMimeData>
#include "folderview_p.h"

namespace Fm {

FolderViewListView::FolderViewListView(QWidget* parent):
  QListView(parent) {
}

FolderViewListView::~FolderViewListView() {
}

void FolderViewListView::startDrag(Qt::DropActions supportedActions) {
  if(movement() != Static)
    QListView::startDrag(supportedActions);
  else
    QAbstractItemView::startDrag(supportedActions);
}

void FolderViewListView::mousePressEvent(QMouseEvent* event) {
  QListView::mousePressEvent(event);
  static_cast<FolderView*>(parent())->childMousePressEvent(event);
}

QModelIndex FolderViewListView::indexAt(const QPoint& point) const {
  QModelIndex index = QListView::indexAt(point);
  // NOTE: QListView has a severe design flaw here. It does hit-testing based on the 
  // total bound rect of the item. The width of an item is determined by max(icon_width, text_width).
  // So if the text label is much wider than the icon, when you click outside the icon but
  // the point is still within the outer bound rect, the item is still selected.
  // This results in very poor usability. Let's do precise hit-testing here.
  // An item is hit only when the point is in the icon or text label.
  // If the point is in the bound rectangle but outside the icon or text, it should not be selected.
  if(viewMode() == QListView::IconMode && index.isValid()) {
    // FIXME: this hack only improves the usability partially. We still need more precise sizeHint handling.
    // FolderItemDelegate* delegate = static_cast<FolderItemDelegate*>(itemDelegateForColumn(FolderModel::ColumnFileName));
    // Q_ASSERT(delegate != NULL);
    // We use the grid size - (2, 2) as the size of the bounding rectangle of the whole item.
    // The width of the text label hence is gridSize.width - 2, and the width and height of the icon is from iconSize().
    QRect visRect = visualRect(index); // visibal area on the screen
    QSize itemSize = gridSize();
    itemSize.setWidth(itemSize.width() - 2);
    itemSize.setHeight(itemSize.height() - 2);
    QSize _iconSize = iconSize();
    int textHeight = itemSize.height() - _iconSize.height();
    if(point.y() < visRect.bottom() - textHeight) {
      // the point is in the icon area, not over the text label
      int iconXMargin = (itemSize.width() - _iconSize.width()) / 2;
      if(point.x() < (visRect.left() + iconXMargin) || point.x() > (visRect.right() - iconXMargin))
	return QModelIndex();
    }
    // qDebug() << "visualRect: " << visRect << "point:" << point;
  }
  return index;
}


// NOTE:
// QListView has a problem which I consider a bug or a design flaw.
// When you set movement property to Static, theoratically the icons
// should not be movable. However, if you turned on icon mode,
// the icons becomes freely movable despite the value of movement is Static.
// To overcome this bug, we override all drag handling methods, and
// call QAbstractItemView directly, bypassing QListView.
// In this way, we can workaround the buggy behavior.
// The drag handlers of QListView basically does the same things
// as its parent QAbstractItemView, but it also stores the currently
// dragged item and paint them in the view as needed.
// TODO: I really should file a bug report to Qt developers.

void FolderViewListView::dragEnterEvent(QDragEnterEvent* event) {
  if(movement() != Static)
    QListView::dragEnterEvent(event);
  else
    QAbstractItemView::dragEnterEvent(event);
  qDebug("dragEnterEvent");
  //static_cast<FolderView*>(parent())->childDragEnterEvent(event);
}

void FolderViewListView::dragLeaveEvent(QDragLeaveEvent* e) {
  if(movement() != Static)
    QListView::dragLeaveEvent(e);
  else
    QAbstractItemView::dragLeaveEvent(e);
  static_cast<FolderView*>(parent())->childDragLeaveEvent(e);
}

void FolderViewListView::dragMoveEvent(QDragMoveEvent* e) {
  if(movement() != Static)
    QListView::dragMoveEvent(e);
  else
    QAbstractItemView::dragMoveEvent(e);
  static_cast<FolderView*>(parent())->childDragMoveEvent(e);
}

void FolderViewListView::dropEvent(QDropEvent* e) {

  static_cast<FolderView*>(parent())->childDropEvent(e);

  if(movement() != Static)
    QListView::dropEvent(e);
  else
    QAbstractItemView::dropEvent(e);
}

//-----------------------------------------------------------------------------

FolderViewTreeView::FolderViewTreeView(QWidget* parent):
  QTreeView(parent),
  layoutTimer_(NULL),
  doingLayout_(false) {

  header()->setStretchLastSection(false);
  setIndentation(0);
}

FolderViewTreeView::~FolderViewTreeView() {
  if(layoutTimer_)
    delete layoutTimer_;
}

void FolderViewTreeView::setModel(QAbstractItemModel* model) {
  QTreeView::setModel(model);
  layoutColumns();
}

void FolderViewTreeView::mousePressEvent(QMouseEvent* event) {
  QTreeView::mousePressEvent(event);
  static_cast<FolderView*>(parent())->childMousePressEvent(event);
}

void FolderViewTreeView::dragEnterEvent(QDragEnterEvent* event) {
  QTreeView::dragEnterEvent(event);
  static_cast<FolderView*>(parent())->childDragEnterEvent(event);
}

void FolderViewTreeView::dragLeaveEvent(QDragLeaveEvent* e) {
  QTreeView::dragLeaveEvent(e);
  static_cast<FolderView*>(parent())->childDragLeaveEvent(e);
}

void FolderViewTreeView::dragMoveEvent(QDragMoveEvent* e) {
  QTreeView::dragMoveEvent(e);
  static_cast<FolderView*>(parent())->childDragMoveEvent(e);
}

void FolderViewTreeView::dropEvent(QDropEvent* e) {
  static_cast<FolderView*>(parent())->childDropEvent(e);
  QTreeView::dropEvent(e);
}

// the default list mode of QListView handles column widths
// very badly (worse than gtk+) and it's not very flexible.
// so, let's handle column widths outselves.
void FolderViewTreeView::layoutColumns() {
  // qDebug("layoutColumns");
  if(!model())
    return;
  doingLayout_ = true;
  QHeaderView* headerView = header();
  // the width that's available for showing the columns.
  int availWidth = viewport()->contentsRect().width();
  int desiredWidth = 0;

  // get the width that every column want
  int numCols = headerView->count();
  int* widths = new int[numCols]; // array to store the widths every column needs
  int column;
  for(column = 0; column < numCols; ++column) {
    int columnId = headerView->logicalIndex(column);
    // get the size that the column needs
    widths[column] = sizeHintForColumn(columnId);
  }

  // the best case is every column can get its full width
  for(column = 0; column < numCols; ++column) {
    desiredWidth += widths[column];
  }

  // if the total witdh we want exceeds the available space
  if(desiredWidth > availWidth) {
    // we don't have that much space for every column
    int filenameColumn = headerView->visualIndex(FolderModel::ColumnFileName);
    // shrink the filename column first
    desiredWidth -= widths[filenameColumn]; // total width of all other columns

    // see if setting the width of the filename column to 200 solve the problem
    if(desiredWidth + 200 > availWidth) {
      // even when we reduce the width of the filename column to 200,
      // the available space is not enough. So we give up trying.
      widths[filenameColumn] = 200;
      desiredWidth += 200;
    }
    else { // we still have more space, so the width of filename column can be increased
      // expand the filename column to fill all available space.
      widths[filenameColumn] = availWidth - desiredWidth;
      desiredWidth = availWidth;
    }
  }

  // really do the resizing for every column
  for(int column = 0; column < numCols; ++column) {
    headerView->resizeSection(column, widths[column]);
  }

  delete []widths;
  doingLayout_ = false;
  
  if(layoutTimer_) {
    delete layoutTimer_;
    layoutTimer_ = NULL;
  }
}

void FolderViewTreeView::resizeEvent(QResizeEvent* event) {
  QAbstractItemView::resizeEvent(event);
  if(!doingLayout_) // prevent endless recursion.
    layoutColumns(); // layoutColumns() also triggers resizeEvent
}

void FolderViewTreeView::rowsInserted(const QModelIndex& parent, int start, int end) {
  QTreeView::rowsInserted(parent, start, end);
  queueLayoutColumns();
}

void FolderViewTreeView::rowsAboutToBeRemoved(const QModelIndex& parent, int start, int end) {
  QTreeView::rowsAboutToBeRemoved(parent, start, end);
  queueLayoutColumns();
}

void FolderViewTreeView::dataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight) {
  QTreeView::dataChanged(topLeft, bottomRight);
  // FIXME: this will be very inefficient
  // queueLayoutColumns();
}

void FolderViewTreeView::queueLayoutColumns() {
  // qDebug("queueLayoutColumns");
  if(!layoutTimer_) {
    layoutTimer_ = new QTimer();
    layoutTimer_->setSingleShot(true);
    layoutTimer_->setInterval(0);
    connect(layoutTimer_, SIGNAL(timeout()), SLOT(layoutColumns()));
  }
  layoutTimer_->start();
}

//-----------------------------------------------------------------------------

FolderView::FolderView(ViewMode _mode, QWidget* parent):
  QWidget(parent),
  view(NULL),
  mode((ViewMode)0),
  fileLauncher_(NULL),
  model_(NULL) {

  iconSize_[IconMode - FirstViewMode] = QSize(48, 48);
  iconSize_[CompactMode - FirstViewMode] = QSize(24, 24);
  iconSize_[ThumbnailMode - FirstViewMode] = QSize(128, 128);
  iconSize_[DetailedListMode - FirstViewMode] = QSize(24, 24);

  QVBoxLayout* layout = new QVBoxLayout();
  layout->setMargin(0);
  setLayout(layout);

  setViewMode(_mode);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  connect(this, SIGNAL(clicked(int, FmFileInfo*)), SLOT(onFileClicked(int, FmFileInfo*)));
}

FolderView::~FolderView() {
  // qDebug("delete FolderView");
}

void FolderView::onItemActivated(QModelIndex index) {
  if(index.isValid() && index.model()) {
    QVariant data = index.model()->data(index, FolderModel::FileInfoRole);
    FmFileInfo* info = (FmFileInfo*)data.value<void*>();
    if(info) {
      Q_EMIT clicked(ActivatedClick, info);
    }
  }
}

void FolderView::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
  QItemSelectionModel* selModel = static_cast<QItemSelectionModel*>(sender());
  int nSel = 0;
  if(viewMode() == DetailedListMode)
    nSel = selModel->selectedRows().count();
  else
    nSel = selModel->selectedIndexes().count();
  // qDebug()<<"selected:" << nSel;
  Q_EMIT selChanged(nSel); // FIXME: this is inefficient
}


void FolderView::setViewMode(ViewMode _mode) {
  if(_mode == mode) // if it's the same more, ignore
    return;
  // FIXME: retain old selection

  // since only detailed list mode uses QTreeView, and others 
  // all use QListView, it's wise to preserve QListView when possible.
  bool recreateView = false;
  if(view && (mode == DetailedListMode || _mode == DetailedListMode)) {
    delete view; // FIXME: no virtual dtor?
    view = NULL;
    recreateView = true;
  }
  mode = _mode;
  QSize iconSize = iconSize_[mode - FirstViewMode];

  if(mode == DetailedListMode) {
    FolderViewTreeView* treeView = new FolderViewTreeView(this);
    view = treeView;
    treeView->setItemsExpandable(false);
    treeView->setRootIsDecorated(false);
    treeView->setAllColumnsShowFocus(false);

    // set our own custom delegate
    FolderItemDelegate* delegate = new FolderItemDelegate(treeView);
    treeView->setItemDelegateForColumn(FolderModel::ColumnFileName, delegate);
  }
  else {
    FolderViewListView* listView;
    if(view)
      listView = static_cast<FolderViewListView*>(view);
    else {
      listView = new FolderViewListView(this);
      view = listView;
    }
    // set our own custom delegate
    FolderItemDelegate* delegate = new FolderItemDelegate(listView);
    listView->setItemDelegateForColumn(FolderModel::ColumnFileName, delegate);
    // FIXME: should we expose the delegate?
    listView->setMovement(QListView::Static);
    listView->setResizeMode(QListView::Adjust);
    listView->setWrapping(true);
    switch(mode) {
      case IconMode: {
        listView->setViewMode(QListView::IconMode);
        // listView->setGridSize(QSize(iconSize.width() * 1.6, iconSize.height() * 2));
        listView->setGridSize(QSize(90, 110));
        listView->setWordWrap(true);
        listView->setFlow(QListView::LeftToRight);
        break;
      }
      case CompactMode: {
        listView->setViewMode(QListView::ListMode);
        listView->setGridSize(QSize());
        listView->setWordWrap(false);
        listView->setFlow(QListView::QListView::TopToBottom);
        break;
      }
      case ThumbnailMode: {
        listView->setViewMode(QListView::IconMode);
        listView->setGridSize(QSize(160, 160));
        listView->setWordWrap(true);
        listView->setFlow(QListView::LeftToRight);
        break;
      }
      default:;
    }
    delegate->setGridSize(listView->gridSize());
  }
  if(view) {
    connect(view, SIGNAL(activated(QModelIndex)), SLOT(onItemActivated(QModelIndex)));
    view->setContextMenuPolicy(Qt::NoContextMenu); // defer the context menu handling to parent widgets
    view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    view->setIconSize(iconSize);

    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout()->addWidget(view);

    // enable dnd
    view->setDragEnabled(true);
    view->setAcceptDrops(true);
    view->setDragDropMode(QAbstractItemView::DragDrop);
    view->setDropIndicatorShown(true);

    if(model_) {
      // FIXME: preserve selections
      model_->setThumbnailSize(iconSize.width());
      view->setModel(model_);
      if(recreateView)
        connect(view->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), SLOT(onSelectionChanged(QItemSelection,QItemSelection)));
    }
  }
}

void FolderView::setIconSize(ViewMode mode, QSize size) {
  Q_ASSERT(mode >= FirstViewMode && mode <= LastViewMode);
  iconSize_[mode - FirstViewMode] = size;
  if(viewMode() == mode) {
    view->setIconSize(size);
    if(model_)
      model_->setThumbnailSize(size.width());
  }
}

QSize FolderView::iconSize(ViewMode mode) const {
  Q_ASSERT(mode >= FirstViewMode && mode <= LastViewMode);
  return iconSize_[mode - FirstViewMode];
}

FolderView::ViewMode FolderView::viewMode() const {
  return mode;
}

QAbstractItemView* FolderView::childView() const {
  return view;
}

ProxyFolderModel* FolderView::model() const {
  return model_;
}

void FolderView::setModel(ProxyFolderModel* model) {
  if(view) {
    view->setModel(model);
    QSize iconSize = iconSize_[mode - FirstViewMode];
    model->setThumbnailSize(iconSize.width());
    if(view->selectionModel())
      connect(view->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), SLOT(onSelectionChanged(QItemSelection,QItemSelection)));
  }
  if(model_)
    delete model_;
  model_ = model;
}

void FolderView::contextMenuEvent(QContextMenuEvent* event) {
  QWidget::contextMenuEvent(event);
  QPoint pos = event->pos();
  QPoint pos2 = view->mapFromParent(pos);
  emitClickedAt(ContextMenuClick, pos2);
}

void FolderView::childMousePressEvent(QMouseEvent* event) {
  // called from mousePressEvent() of chld view
  if(event->button() == Qt::MiddleButton) {
    QPoint pos = event->pos();
    emitClickedAt(MiddleClick, pos);
  }
}

void FolderView::emitClickedAt(ClickType type, QPoint& pos) {
  QPoint viewport_pos = view->viewport()->mapFromParent(pos);
  // indexAt() needs a point in "viewport" coordinates.
  QModelIndex index = view->indexAt(viewport_pos);
  if(index.isValid()) {
    QVariant data = index.data(FolderModel::FileInfoRole);
    FmFileInfo* info = reinterpret_cast<FmFileInfo*>(data.value<void*>());
    Q_EMIT clicked(type, info);
  }
  else {
    // FIXME: should we show popup menu for the selected files instead
    // if there are selected files?
    if(type == ContextMenuClick) {
      // clear current selection if clicked outside selected files
      view->clearSelection();
      Q_EMIT clicked(type, NULL);
    }
  }
}

QModelIndexList FolderView::selectedRows(int column) const {
  QItemSelectionModel* selModel = selectionModel();
  if(selModel) {
    return selModel->selectedRows();
  }
  return QModelIndexList();
}

// This returns all selected "cells", which means all cells of the same row are returned.
QModelIndexList FolderView::selectedIndexes() const {
  QItemSelectionModel* selModel = selectionModel();
  if(selModel) {
    return selModel->selectedIndexes();
  }
  return QModelIndexList();
}

QItemSelectionModel* FolderView::selectionModel() const {
  return view ? view->selectionModel() : NULL;
}

FmPathList* FolderView::selectedFilePaths() const {
  if(model_) {
    QModelIndexList selIndexes = mode == DetailedListMode ? selectedRows() : selectedIndexes();
    if(!selIndexes.isEmpty()) {
      FmPathList* paths = fm_path_list_new();
      QModelIndexList::const_iterator it;
      for(it = selIndexes.begin(); it != selIndexes.end(); ++it) {
        FmFileInfo* file = model_->fileInfoFromIndex(*it);
        fm_path_list_push_tail(paths, fm_file_info_get_path(file));
      }
      return paths;
    }
  }
  return NULL;
}

FmFileInfoList* FolderView::selectedFiles() const {
  if(model_) {
    QModelIndexList selIndexes = mode == DetailedListMode ? selectedRows() : selectedIndexes();
    if(!selIndexes.isEmpty()) {
      FmFileInfoList* files = fm_file_info_list_new();
      QModelIndexList::const_iterator it;
      for(it = selIndexes.constBegin(); it != selIndexes.constEnd(); it++) {
        FmFileInfo* file = model_->fileInfoFromIndex(*it);
        fm_file_info_list_push_tail(files, file);
      }
      return files;
    }
  }
  return NULL;
}

void FolderView::invertSelection() {
  if(model_) {
    QItemSelectionModel* selModel = view->selectionModel();
    int rows = model_->rowCount();
    for(int row = 0; row < rows; ++row) {
      QModelIndex index = model_->index(row, 0);
      selModel->select(index, QItemSelectionModel::Toggle|QItemSelectionModel::Rows);
    }
  }
}

void FolderView::childDragEnterEvent(QDragEnterEvent* event) {
  qDebug("drag enter");
  if(event->mimeData()->hasFormat("text/uri-list")) {
    event->accept();
  }
  else
    event->ignore();
}

void FolderView::childDragLeaveEvent(QDragLeaveEvent* e) {
  qDebug("drag leave");
  e->accept();
}

void FolderView::childDragMoveEvent(QDragMoveEvent* e) {
  qDebug("drag move");
}

void FolderView::childDropEvent(QDropEvent* e) {
  qDebug("drop");
  if(e->keyboardModifiers() == Qt::NoModifier) {
    // if no key modifiers are used, popup a menu
    // to ask the user for the action he/she wants to perform.
    Qt::DropAction action = DndActionMenu::askUser(QCursor::pos());
    e->setDropAction(action);
  }
}

void FolderView::onFileClicked(int type, FmFileInfo* fileInfo) {
  if(type == ActivatedClick) {
    if(fileLauncher_) {
      GList* files = g_list_append(NULL, fileInfo);
      fileLauncher_->launchFiles(NULL, files);
      g_list_free(files);
    }
  }
  else if(type == ContextMenuClick) {
    FmPath* folderPath = path();
    QMenu* menu;
    if(fileInfo) {
      // show context menu
      FmFileInfoList* files = selectedFiles();
      Fm::FileMenu* fileMenu = new Fm::FileMenu(files, fileInfo, folderPath);
      fileMenu->setFileLauncher(fileLauncher_);
      prepareFileMenu(fileMenu);
      fm_file_info_list_unref(files);
      menu = fileMenu;
    }
    else {
      FmFolder* _folder = folder();
      FmFileInfo* info = fm_folder_get_info(_folder);
      Fm::FolderMenu* folderMenu = new Fm::FolderMenu(this);
      prepareFolderMenu(folderMenu);
      menu = folderMenu;
    }
    menu->popup(QCursor::pos());
    connect(menu, SIGNAL(aboutToHide()), menu, SLOT(deleteLater()));
  }
}

void FolderView::prepareFileMenu(FileMenu* menu) {
}

void FolderView::prepareFolderMenu(FolderMenu* menu) {
}


} // namespace Fm
