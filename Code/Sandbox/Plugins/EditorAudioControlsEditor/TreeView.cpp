// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "TreeView.h"

#include <CryAudio/IAudioSystem.h>

#include <QHeaderView>

namespace ACE
{
//////////////////////////////////////////////////////////////////////////
CTreeView::CTreeView(QWidget* const pParent, QAdvancedTreeView::BehaviorFlags const flags /*= QAdvancedTreeView::BehaviorFlags(UseItemModelAttribute)*/)
	: QAdvancedTreeView(QAdvancedTreeView::BehaviorFlags(flags), pParent)
{
	QObject::connect(header(), &QHeaderView::sortIndicatorChanged, [this] () { scrollTo(currentIndex()); });
}

//////////////////////////////////////////////////////////////////////////
bool CTreeView::IsEditing() const
{
	return state() == QAbstractItemView::EditingState;
}

//////////////////////////////////////////////////////////////////////////
void CTreeView::ExpandSelection(QModelIndexList const& indexList)
{
	for (auto const& index : indexList)
	{
		if (index.isValid())
		{
			int const childCount = index.model()->rowCount(index);

			for (int i = 0; i < childCount; ++i)
			{
				QModelIndexList childList;
				childList.append(index.child(i, 0));
				ExpandSelection(childList);
			}

			expand(index);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CTreeView::CollapseSelection(QModelIndexList const& indexList)
{
	for (auto const& index : indexList)
	{
		if (index.isValid())
		{
			int const childCount = index.model()->rowCount(index);

			for (int i = 0; i < childCount; ++i)
			{
				QModelIndexList childList;
				childList.append(index.child(i, 0));
				CollapseSelection(childList);
			}

			collapse(index);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
uint32 CTreeView::GetItemId(QModelIndex const& index) const
{
	uint32 itemId = CryAudio::InvalidCRC32;

	if (index.isValid())
	{
		QModelIndex const itemIndex = index.sibling(index.row(), m_nameColumn);

		if (itemIndex.isValid())
		{
			QString itemName = itemIndex.data(m_nameRole).toString();
			QModelIndex parent = itemIndex.parent();

			while (parent.isValid())
			{
				itemName.prepend((parent.data(m_nameRole).toString()) + "/");
				parent = parent.parent();
			}

			itemId = CryAudio::StringToId(itemName.toStdString().c_str());
		}
	}

	return itemId;
}

//////////////////////////////////////////////////////////////////////////
void CTreeView::BackupExpanded()
{
	m_expandedBackup.clear();

	int const rowCount = model()->rowCount();

	for (int i = 0; i < rowCount; ++i)
	{
		BackupExpandedChildren(model()->index(i, 0));
	}
}

//////////////////////////////////////////////////////////////////////////
void CTreeView::BackupExpandedChildren(QModelIndex const& index)
{
	if (index.isValid())
	{
		int const rowCount = model()->rowCount(index);

		if (rowCount > 0)
		{
			for (int i = 0; i < rowCount; ++i)
			{
				BackupExpandedChildren(index.child(i, 0));
			}
		}

		if (isExpanded(index))
		{
			m_expandedBackup.insert(GetItemId(index));
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CTreeView::RestoreExpanded()
{
	if (!m_expandedBackup.isEmpty())
	{
		int const rowCount = model()->rowCount();

		for (int i = 0; i < rowCount; ++i)
		{
			RestoreExpandedChildren(model()->index(i, 0));
		}

		m_expandedBackup.clear();
	}
}

//////////////////////////////////////////////////////////////////////////
void CTreeView::RestoreExpandedChildren(QModelIndex const& index)
{
	if (index.isValid())
	{
		int const rowCount = model()->rowCount(index);

		if (rowCount > 0)
		{
			for (int i = 0; i < rowCount; ++i)
			{
				RestoreExpandedChildren(index.child(i, 0));
			}
		}

		if (m_expandedBackup.contains(GetItemId(index)))
		{
			expand(index);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CTreeView::BackupSelection()
{
	m_selectionBackup.clear();

	QModelIndexList const& selectedList = selectionModel()->selectedRows();

	for (QModelIndex const& index : selectedList)
	{
		m_selectionBackup.insert(GetItemId(index));
	}
}

//////////////////////////////////////////////////////////////////////////
void CTreeView::RestoreSelection()
{
	if (!m_selectionBackup.isEmpty())
	{
		int const rowCount = model()->rowCount();

		for (int i = 0; i < rowCount; ++i)
		{
			RestoreSelectionChildren(model()->index(i, 0));
		}
	}
	else
	{
		for (QModelIndex const& index : selectionModel()->selectedIndexes())
		{
			scrollTo(index);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CTreeView::RestoreSelectionChildren(QModelIndex const& index)
{
	if (index.isValid())
	{
		int const rowCount = model()->rowCount(index);

		if (rowCount > 0)
		{
			for (int i = 0; i < rowCount; ++i)
			{
				RestoreSelectionChildren(index.child(i, 0));
			}
		}

		if (m_selectionBackup.contains(GetItemId(index)))
		{
			selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
			scrollTo(index);
		}
	}
}
} // namespace ACE
