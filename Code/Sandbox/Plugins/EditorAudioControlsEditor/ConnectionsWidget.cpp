// Copyright 2001-2018 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include "ConnectionsWidget.h"

#include "SystemAssets.h"
#include "AudioControlsEditorPlugin.h"
#include "ImplementationManager.h"
#include "TreeView.h"
#include "ConnectionsModel.h"

#include <IEditorImpl.h>
#include <ImplItem.h>
#include <IEditor.h>
#include <QtUtil.h>
#include <Controls/QuestionDialog.h>
#include <CrySerialization/IArchive.h>
#include <CrySerialization/STL.h>
#include <Serialization/QPropertyTree/QPropertyTree.h>
#include <ProxyModels/AttributeFilterProxyModel.h>

#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QSplitter>
#include <QVBoxLayout>

namespace ACE
{
//////////////////////////////////////////////////////////////////////////
CConnectionsWidget::CConnectionsWidget(QWidget* const pParent)
	: QWidget(pParent)
	, m_pControl(nullptr)
	, m_pConnectionModel(new CConnectionModel(this))
	, m_pAttributeFilterProxyModel(new QAttributeFilterProxyModel(QAttributeFilterProxyModel::BaseBehavior, this))
	, m_pConnectionProperties(new QPropertyTree(this))
	, m_pTreeView(new CTreeView(this))
	, m_nameColumn(static_cast<int>(CConnectionModel::EColumns::Name))
{
	m_pAttributeFilterProxyModel->setSourceModel(m_pConnectionModel);
	m_pAttributeFilterProxyModel->setFilterKeyColumn(m_nameColumn);

	m_pTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_pTreeView->setDragEnabled(false);
	m_pTreeView->setAcceptDrops(true);
	m_pTreeView->setDragDropMode(QAbstractItemView::DropOnly);
	m_pTreeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_pTreeView->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_pTreeView->setUniformRowHeights(true);
	m_pTreeView->setContextMenuPolicy(Qt::CustomContextMenu);
	m_pTreeView->setModel(m_pAttributeFilterProxyModel);
	m_pTreeView->sortByColumn(m_nameColumn, Qt::AscendingOrder);
	m_pTreeView->setItemsExpandable(false);
	m_pTreeView->setRootIsDecorated(false);
	m_pTreeView->installEventFilter(this);
	m_pTreeView->header()->setMinimumSectionSize(25);
	m_pTreeView->header()->setSectionResizeMode(static_cast<int>(CConnectionModel::EColumns::Notification), QHeaderView::ResizeToContents);
	m_pTreeView->SetNameColumn(m_nameColumn);
	m_pTreeView->SetNameRole(static_cast<int>(CConnectionModel::ERoles::Name));
	m_pTreeView->TriggerRefreshHeaderColumns();

	QObject::connect(m_pTreeView, &CTreeView::customContextMenuRequested, this, &CConnectionsWidget::OnContextMenu);
	QObject::connect(m_pTreeView->selectionModel(), &QItemSelectionModel::selectionChanged, [this]()
		{
			RefreshConnectionProperties();
			UpdateSelectedConnections();
	  });

	QSplitter* const pSplitter = new QSplitter(Qt::Vertical, this);
	pSplitter->addWidget(m_pTreeView);
	pSplitter->addWidget(m_pConnectionProperties);
	pSplitter->setCollapsible(0, false);
	pSplitter->setCollapsible(1, false);

	QVBoxLayout* const pMainLayout = new QVBoxLayout(this);
	pMainLayout->setContentsMargins(0, 0, 0, 0);
	pMainLayout->addWidget(pSplitter);
	setLayout(pMainLayout);

	setHidden(true);

	CAudioControlsEditorPlugin::GetAssetsManager()->SignalConnectionRemoved.Connect([&](CSystemControl* pControl)
		{
			if (!CAudioControlsEditorPlugin::GetAssetsManager()->IsLoading() && (m_pControl == pControl))
			{
			  // clear the selection if a connection is removed
			  m_pTreeView->selectionModel()->clear();
			  RefreshConnectionProperties();
			}
	  }, reinterpret_cast<uintptr_t>(this));

	CAudioControlsEditorPlugin::GetImplementationManger()->SignalImplementationAboutToChange.Connect([&]()
		{
			m_pTreeView->selectionModel()->clear();
			RefreshConnectionProperties();
	  }, reinterpret_cast<uintptr_t>(this));

	QObject::connect(m_pConnectionModel, &CConnectionModel::SignalConnectionAdded, this, &CConnectionsWidget::OnConnectionAdded);
}

//////////////////////////////////////////////////////////////////////////
CConnectionsWidget::~CConnectionsWidget()
{
	CAudioControlsEditorPlugin::GetAssetsManager()->SignalConnectionRemoved.DisconnectById(reinterpret_cast<uintptr_t>(this));
	CAudioControlsEditorPlugin::GetImplementationManger()->SignalImplementationAboutToChange.DisconnectById(reinterpret_cast<uintptr_t>(this));

	m_pConnectionModel->DisconnectSignals();
	m_pConnectionModel->deleteLater();
}

//////////////////////////////////////////////////////////////////////////
bool CConnectionsWidget::eventFilter(QObject* pObject, QEvent* pEvent)
{
	bool isEvent = false;

	if (pEvent->type() == QEvent::KeyPress)
	{
		QKeyEvent const* const pKeyEvent = static_cast<QKeyEvent*>(pEvent);

		if ((pKeyEvent != nullptr) && (pKeyEvent->key() == Qt::Key_Delete) && (pObject == m_pTreeView))
		{
			RemoveSelectedConnection();
			isEvent = true;
		}
	}

	if (!isEvent)
	{
		isEvent = QWidget::eventFilter(pObject, pEvent);
	}

	return isEvent;
}

//////////////////////////////////////////////////////////////////////////
void CConnectionsWidget::OnContextMenu(QPoint const& pos)
{
	auto const selection = m_pTreeView->selectionModel()->selectedRows();
	int const selectionCount = selection.count();

	if (selectionCount > 0)
	{
		QMenu* const pContextMenu = new QMenu(this);

		char const* actionName = "Remove Connection";

		if (selectionCount > 1)
		{
			actionName = "Remove Connections";
		}

		pContextMenu->addAction(tr(actionName), [&]() { RemoveSelectedConnection(); });

		if (selectionCount == 1)
		{
			IEditorImpl const* const pEditorImpl = CAudioControlsEditorPlugin::GetImplEditor();

			if (pEditorImpl != nullptr)
			{
				CID const itemId = selection[0].data(static_cast<int>(CConnectionModel::ERoles::Id)).toInt();
				CImplItem const* const pImplControl = pEditorImpl->GetControl(itemId);

				if ((pImplControl != nullptr) && !pImplControl->IsPlaceholder())
				{
					pContextMenu->addSeparator();
					pContextMenu->addAction(tr("Select in Middleware Data"), [=]()
						{
							SignalSelectConnectedImplItem(itemId);
					  });
				}
			}
		}

		pContextMenu->exec(QCursor::pos());
	}
}

//////////////////////////////////////////////////////////////////////////
void CConnectionsWidget::OnConnectionAdded(CID const id)
{
	if (m_pControl != nullptr)
	{
		auto const& matches = m_pAttributeFilterProxyModel->match(m_pAttributeFilterProxyModel->index(0, 0, QModelIndex()), static_cast<int>(CConnectionModel::ERoles::Id), id, 1, Qt::MatchRecursive);

		if (!matches.isEmpty())
		{
			m_pTreeView->selectionModel()->select(matches.first(), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		}

		m_pTreeView->resizeColumnToContents(m_nameColumn);
	}
}

//////////////////////////////////////////////////////////////////////////
void CConnectionsWidget::RemoveSelectedConnection()
{
	if (m_pControl != nullptr)
	{
		CQuestionDialog* const messageBox = new CQuestionDialog();
		QModelIndexList const& selectedIndexes = m_pTreeView->selectionModel()->selectedRows(m_nameColumn);

		if (!selectedIndexes.empty())
		{
			int const size = selectedIndexes.length();
			QString text;

			if (size == 1)
			{
				text = R"(Are you sure you want to delete the connection between ")" + QtUtil::ToQString(m_pControl->GetName()) + R"(" and ")" + selectedIndexes[0].data(Qt::DisplayRole).toString() + R"("?)";
			}
			else
			{
				text = "Are you sure you want to delete the " + QString::number(size) + " selected connections?";
			}

			messageBox->SetupQuestion("Audio Controls Editor", text);

			if (messageBox->Execute() == QDialogButtonBox::Yes)
			{
				IEditorImpl const* const pEditorImpl = CAudioControlsEditorPlugin::GetImplEditor();

				if (pEditorImpl != nullptr)
				{
					std::vector<CImplItem*> implItems;
					implItems.reserve(selectedIndexes.size());

					for (QModelIndex const& index : selectedIndexes)
					{
						CID const id = index.data(static_cast<int>(CConnectionModel::ERoles::Id)).toInt();
						implItems.emplace_back(pEditorImpl->GetControl(id));
					}

					for (CImplItem* const pImplItem : implItems)
					{
						if (pImplItem != nullptr)
						{
							m_pControl->RemoveConnection(pImplItem);
						}
					}
				}

				m_pTreeView->resizeColumnToContents(m_nameColumn);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CConnectionsWidget::SetControl(CSystemControl* const pControl)
{
	if (m_pControl != pControl)
	{
		m_pControl = pControl;
		Reload();

		if (m_pControl != nullptr)
		{
			auto const& selectedConnections = m_pControl->GetSelectedConnections();

			if (!selectedConnections.empty())
			{
				int matchCount = 0;

				for (auto const itemId : selectedConnections)
				{
					auto const& matches = m_pAttributeFilterProxyModel->match(m_pAttributeFilterProxyModel->index(0, 0, QModelIndex()), static_cast<int>(CConnectionModel::ERoles::Id), itemId, 1, Qt::MatchRecursive);

					if (!matches.isEmpty())
					{
						m_pTreeView->selectionModel()->select(matches.first(), QItemSelectionModel::Select | QItemSelectionModel::Rows);
						++matchCount;
					}
				}

				if (matchCount == 0)
				{
					m_pTreeView->setCurrentIndex(m_pTreeView->model()->index(0, m_nameColumn));
				}
			}
			else
			{
				m_pTreeView->setCurrentIndex(m_pTreeView->model()->index(0, m_nameColumn));
			}
		}

		m_pTreeView->resizeColumnToContents(m_nameColumn);
	}
}

//////////////////////////////////////////////////////////////////////////
void CConnectionsWidget::Reload()
{
	m_pConnectionModel->Init(m_pControl);
	m_pTreeView->selectionModel()->clear();
	RefreshConnectionProperties();
}

//////////////////////////////////////////////////////////////////////////
void CConnectionsWidget::RefreshConnectionProperties()
{
	ConnectionPtr pConnection;

	if (m_pControl != nullptr)
	{
		QModelIndexList const& selectedIndexes = m_pTreeView->selectionModel()->selectedRows(m_nameColumn);

		if (!selectedIndexes.empty())
		{
			QModelIndex const& index = selectedIndexes[0];

			if (index.isValid())
			{
				CID const id = index.data(static_cast<int>(CConnectionModel::ERoles::Id)).toInt();
				pConnection = m_pControl->GetConnection(id);
			}
		}
	}

	if ((pConnection != nullptr) && pConnection->HasProperties())
	{
		m_pConnectionProperties->attach(Serialization::SStruct(*pConnection.get()));
		m_pConnectionProperties->setHidden(false);
	}
	else
	{
		m_pConnectionProperties->detach();
		m_pConnectionProperties->setHidden(true);
	}
}

//////////////////////////////////////////////////////////////////////////
void CConnectionsWidget::UpdateSelectedConnections()
{
	std::vector<CID> currentSelection;
	QModelIndexList const& selectedIndexes = m_pTreeView->selectionModel()->selectedRows(m_nameColumn);

	for (auto const& index : selectedIndexes)
	{
		if (index.isValid())
		{
			CID const id = index.data(static_cast<int>(CConnectionModel::ERoles::Id)).toInt();
			currentSelection.emplace_back(id);
		}
	}

	m_pControl->SetSelectedConnections(currentSelection);
}

//////////////////////////////////////////////////////////////////////////
void CConnectionsWidget::BackupTreeViewStates()
{
	m_pTreeView->BackupSelection();
}

//////////////////////////////////////////////////////////////////////////
void CConnectionsWidget::RestoreTreeViewStates()
{
	m_pTreeView->RestoreSelection();
}
} // namespace ACE
