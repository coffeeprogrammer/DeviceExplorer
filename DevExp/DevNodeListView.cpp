#include "pch.h"
#include "DevNodeListView.h"
#include "Helpers.h"
#include "SortHelper.h"

void CDevNodeListView::Refresh() {
	m_dm = DeviceManager::Create();
	m_Items = m_dm->EnumDevices<DeviceItem>();
	m_List.GetImageList(LVSIL_SMALL).RemoveAll();
	m_List.SetItemCountEx((int)m_Items.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
}

CString CDevNodeListView::GetColumnText(HWND, int row, int col) {
	auto& item = m_Items[row];
	switch (GetColumnManager(m_List)->GetColumnTag<ColumnType>(col)) {
		case ColumnType::Name: return item.Description.c_str();
		case ColumnType::Instance: return std::to_wstring(item.Data.DevInst).c_str();
		case ColumnType::Class:
			return GetDeviceClassName(item);
		case ColumnType::Status:
			return Helpers::DevNodeStatusToString(DeviceNode(item.Data.DevInst).GetStatus());
		case ColumnType::Driver:
			return GetStringProperty(item, DEVPKEY_Device_Driver);
		case ColumnType::PDOName:
			return GetStringProperty(item, DEVPKEY_Device_PDOName);
		case ColumnType::Parent:
			return GetStringProperty(item, DEVPKEY_Device_Parent);
		case ColumnType::Enumerator:
			return GetStringProperty(item, DEVPKEY_Device_EnumeratorName);
	}
	return L"";
}

LRESULT CDevNodeListView::OnCreate(UINT, WPARAM, LPARAM, BOOL&) {
	m_hWndClient = m_List.Create(m_hWnd, rcDefault, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN
		| LVS_OWNERDATA | LVS_REPORT | LVS_SHOWSELALWAYS);
	m_List.SetExtendedListViewStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
	CImageList images;
	images.Create(16, 16, ILC_COLOR32 | ILC_MASK, 64, 32);
	m_List.SetImageList(images, LVSIL_SMALL);

	auto cm = GetColumnManager(m_List);
	cm->AddColumn(L"Name", LVCFMT_LEFT, 250, ColumnType::Name);
	cm->AddColumn(L"Instance", LVCFMT_RIGHT, 80, ColumnType::Instance);
	cm->AddColumn(L"Class", LVCFMT_LEFT, 140, ColumnType::Class);
	cm->AddColumn(L"Status", LVCFMT_LEFT, 240, ColumnType::Status);
	cm->AddColumn(L"PDO Name", LVCFMT_LEFT, 160, ColumnType::PDOName);
	cm->AddColumn(L"Enumerator", LVCFMT_LEFT, 120, ColumnType::Enumerator);
	cm->AddColumn(L"Parent", LVCFMT_LEFT, 220, ColumnType::Parent);

	Refresh();

    return 0;
}

LRESULT CDevNodeListView::OnSetFocus(UINT, WPARAM, LPARAM, BOOL&) {
    m_List.SetFocus();

    return 0;
}

CString CDevNodeListView::GetStringProperty(DeviceItem& item, DEVPROPKEY const& key) {
	return DeviceNode(item.Data.DevInst).GetProperty<std::wstring>(key).c_str();
}

CString const& CDevNodeListView::GetDeviceClassName(DeviceItem& item) {
	if (item.Class.IsEmpty())
		item.Class = DeviceNode(item.Data.DevInst).GetProperty<std::wstring>(DEVPKEY_Device_Class).c_str();
	return item.Class;
}

int CDevNodeListView::GetRowImage(HWND, int row, int) {
	auto& item = m_Items[row];
	if (item.Image >= 0)
		return item.Image;
	auto icon = m_dm->GetDeviceIcon(item);
	if (icon) {
		item.Image = m_List.GetImageList(LVSIL_SMALL).AddIcon(icon);
		::DestroyIcon(icon);
	}
	return item.Image;
}

void CDevNodeListView::DoSort(SortInfo* const si) {
	auto compare = [&](auto& d1, auto& d2) {
		switch (GetColumnManager(m_List)->GetColumnTag<ColumnType>(si->SortColumn)) {
			case ColumnType::Name: return SortHelper::Sort(d1.Description, d2.Description, si->SortAscending);
			case ColumnType::Class: return SortHelper::Sort(GetDeviceClassName(d1), GetDeviceClassName(d2), si->SortAscending);
			case ColumnType::Instance: return SortHelper::Sort(d1.Data.DevInst, d2.Data.DevInst, si->SortAscending);
			case ColumnType::Status: return SortHelper::Sort(DeviceNode(d1.Data.DevInst).GetStatus(), DeviceNode(d2.Data.DevInst).GetStatus(), si->SortAscending);
			case ColumnType::Enumerator: return SortHelper::Sort(GetStringProperty(d1, DEVPKEY_Device_EnumeratorName), GetStringProperty(d2, DEVPKEY_Device_EnumeratorName), si->SortAscending);
			case ColumnType::PDOName: return SortHelper::Sort(GetStringProperty(d1, DEVPKEY_Device_PDOName), GetStringProperty(d2, DEVPKEY_Device_PDOName), si->SortAscending);
			case ColumnType::Parent: return SortHelper::Sort(GetStringProperty(d1, DEVPKEY_Device_Parent), GetStringProperty(d2, DEVPKEY_Device_Parent), si->SortAscending);
		}
		return false;
	};

	std::sort(m_Items.begin(), m_Items.end(), compare);
	m_List.RedrawItems(m_List.GetTopIndex(), m_List.GetTopIndex() + m_List.GetCountPerPage());
}
