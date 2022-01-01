#include "pch.h"
#include "DeviceManager.h"
#include <RegStr.h>

#pragma comment(lib, "setupapi")
#pragma comment(lib, "cfgmgr32")

SP_CLASSIMAGELIST_DATA g_ClassImageList;

std::unique_ptr<DeviceManager> DeviceManager::Create(const wchar_t* computerName, const GUID* classGuid, const wchar_t* enumerator, InfoSetOptions options) {
	auto dm = new DeviceManager(computerName, classGuid, enumerator, options);
	if (dm->_hInfoSet)
		return std::unique_ptr<DeviceManager>(dm);
	delete dm;
	return nullptr;
}

std::wstring DeviceManager::GetDeviceClassDescription(GUID const& guid, const wchar_t* computerName) {
	wchar_t desc[256];
	if (::SetupDiGetClassDescriptionEx(&guid, desc, _countof(desc), nullptr, computerName, nullptr)) {
		return desc;
	}
	return L"";
}

HIMAGELIST DeviceManager::GetClassImageList() {
	if (g_ClassImageList.ImageList == nullptr) {
		g_ClassImageList.cbSize = sizeof(g_ClassImageList);
		::SetupDiGetClassImageList(&g_ClassImageList);
	}
	return g_ClassImageList.ImageList;
}

int DeviceManager::GetClassImageIndex(GUID const& guid) {
	int index = -1;
	::SetupDiGetClassImageIndex(&g_ClassImageList, &guid, &index);
	return index;
}

std::vector<HardwareProfile> DeviceManager::EnumHardwareProfiles(PCWSTR computerName) {
	std::vector<HardwareProfile> hwprofiles;
	DWORD size;
	if (!::SetupDiGetHwProfileListEx(nullptr, 0, &size, nullptr, computerName, nullptr) && ::GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
		auto profiles = std::make_unique<DWORD[]>(size);
		if (::SetupDiGetHwProfileListEx(profiles.get(), size, &size, nullptr, computerName, nullptr)) {
			WCHAR name[256];
			hwprofiles.reserve(size);
			for (DWORD i = 0; i < size; i++) {
				HardwareProfile profile;
				profile.Index = i;
				if (::SetupDiGetHwProfileFriendlyNameEx(i, name, _countof(name), nullptr, computerName, nullptr))
					profile.FriendlyName = name;
				hwprofiles.push_back(std::move(profile));
			}
		}
	}
	return hwprofiles;
}

std::vector<DEVPROPKEY> DeviceManager::GetDeviceClassPropertyKeys(GUID const& guid) {
	return GetDeviceClassPropertyKeysCommon(guid, true);
}

std::vector<DEVPROPKEY> DeviceManager::GetDeviceInterfacePropertyKeys(GUID const& guid) {
	return GetDeviceClassPropertyKeysCommon(guid, false);
}

DeviceNode DeviceManager::GetRootDeviceNode() {
	DEVINST inst;
	return CR_SUCCESS == ::CM_Locate_DevNode(&inst, nullptr, CM_LOCATE_DEVNODE_NORMAL) ? inst : 0;
}

std::wstring DeviceManager::GetDeviceRegistryPropertyString(const DeviceInfo& di, DeviceRegistryPropertyType type) const {
	std::wstring result;
	result.resize(256);

	DWORD regType;
	if (::SetupDiGetDeviceRegistryProperty(_hInfoSet.get(), (PSP_DEVINFO_DATA)&di.Data, static_cast<DWORD>(type), &regType,
		(BYTE*)result.data(), DWORD(result.size() * sizeof(wchar_t)), nullptr)) {
		assert(regType == REG_SZ);
		return result;
	}
	return L"";
}

std::vector<std::wstring> DeviceManager::GetDeviceRegistryPropertyMultiString(const DeviceInfo& di, DeviceRegistryPropertyType type) const {
	std::vector<std::wstring> result;
	WCHAR buffer[1 << 11];
	DWORD regType;
	auto ok = ::SetupDiGetDeviceRegistryProperty(_hInfoSet.get(), (PSP_DEVINFO_DATA)&di.Data, static_cast<DWORD>(type), &regType, (BYTE*)buffer, sizeof(buffer), nullptr);
	if (!ok)
		return result;

	assert(regType == REG_MULTI_SZ);
	for (auto p = buffer; *p;) {
		result.push_back(p);
		p += ::wcslen(p) + 1;
	}
	return result;
}

HICON DeviceManager::GetDeviceIcon(const DeviceInfo& di, bool big) const {
	HICON hIcon = nullptr;
	auto size = big ? 32 : 16;
	::SetupDiLoadDeviceIcon(_hInfoSet.get(), (PSP_DEVINFO_DATA)&di.Data, size, size, 0, &hIcon);
	return hIcon;
}

std::wstring DeviceManager::GetDeviceClassRegistryPropertyString(const GUID* guid, DeviceClassRegistryPropertyType type) {
	DWORD regType;
	std::wstring result;
	result.resize(256);
	if (::SetupDiGetClassRegistryProperty(guid, static_cast<DWORD>(type), &regType, 
		(BYTE*)result.data(), DWORD(result.size() * sizeof(WCHAR)), nullptr, nullptr, nullptr)) {
		assert(regType == REG_SZ);
		return result;
	}

	return L"";
}

std::vector<std::wstring> DeviceManager::GetDeviceClassRegistryPropertyMultiString(const GUID* guid, DeviceClassRegistryPropertyType type) {
	std::vector<std::wstring> result;
	WCHAR buffer[1 << 12];
	DWORD regType;
	if (::SetupDiGetClassRegistryProperty(guid, static_cast<DWORD>(type), &regType, (BYTE*)buffer, sizeof(buffer), nullptr, nullptr, nullptr)) {
		assert(regType == REG_MULTI_SZ);
		for (auto p = buffer; *p;) {
			result.push_back(p);
			p += ::wcslen(p) + 1;
		}
	}
	return result;
}

std::vector<DeviceClassInfo> DeviceManager::EnumDeviceClasses() {
	GUID guid;
	std::vector<DeviceClassInfo> classes;
	classes.reserve(64);
	WCHAR name[128];
	for (DWORD i = 0;; i++) {
		if (CR_NO_SUCH_VALUE == ::CM_Enumerate_Classes(i, &guid, CM_ENUMERATE_CLASSES_INSTALLER))
			break;

		ULONG len = _countof(name);
		::CM_Get_Class_Name(&guid, name, &len, 0);
		DeviceClassInfo info;
		info.Guid = guid;
		info.Name = name;
		classes.push_back(std::move(info));
	}
	return classes;
}

bool DeviceManager::EnumDeviceInterfaces(GUID const& guid, std::vector<DeviceInterfaceInfo>& vec) {
	SP_DEVICE_INTERFACE_DATA data = { sizeof(data) };
	auto buffer = std::make_unique<BYTE[]>(2048);
	auto detail = reinterpret_cast<SP_INTERFACE_DEVICE_DETAIL_DATA*>(buffer.get());
	SP_DEVINFO_DATA devData = { sizeof(devData) };
	WCHAR name[256];

	DWORD i = 0;
	for (;; i++) {
		if (!::SetupDiEnumDeviceInterfaces(_hInfoSet.get(), nullptr, &guid, i, &data))
			break;

		DeviceInterfaceInfo info;
		info.Guid = data.InterfaceClassGuid;
		detail->cbSize = sizeof(*detail);
		if (::SetupDiGetDeviceInterfaceDetail(_hInfoSet.get(), &data, detail, 2048, nullptr, &devData)
			|| ::GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
			auto hKey = ::SetupDiOpenDeviceInterfaceRegKey(_hInfoSet.get(), &data, 0, KEY_READ);
			if (hKey != INVALID_HANDLE_VALUE) {
				CRegKey key(hKey);
				ULONG chars = _countof(name);
				if (ERROR_SUCCESS == key.QueryStringValue(L"FriendlyName", name, &chars))
					info.FriendlyName = name;
			}
			DeviceInfo di;
			di.Data = devData;
			info.DeviceFriendlyName = GetDeviceRegistryPropertyString(di, DeviceRegistryPropertyType::FriendlyName);
			info.DeviceDescription = GetDeviceRegistryPropertyString(di, DeviceRegistryPropertyType::Description);
			info.SymbolicLink = detail->DevicePath;
		}
		vec.push_back(std::move(info));
	}
	return i > 0;
}

std::vector<DeviceInterfaceInfo> DeviceManager::EnumDeviceInterfaces() {
	GUID guid;
	std::vector<DeviceInterfaceInfo> data;
	data.reserve(32);
	for (DWORD i = 0;; i++) {
		if (CR_NO_SUCH_VALUE == ::CM_Enumerate_Classes(i, &guid, CM_ENUMERATE_CLASSES_INTERFACE))
			break;
		DeviceManager dm(nullptr, &guid, nullptr, InfoSetOptions::DeviceInterface | InfoSetOptions::Present);
		dm.EnumDeviceInterfaces(guid, data);
	}

	return data;
}

DeviceManager::DeviceManager(const wchar_t* computerName, const GUID* classGuid, const wchar_t* enumerator, InfoSetOptions options) {
	_hInfoSet.reset(::SetupDiGetClassDevsEx(classGuid, enumerator, nullptr, static_cast<DWORD>(options), nullptr, computerName, nullptr));
}

std::vector<DEVPROPKEY> DeviceManager::GetDeviceClassPropertyKeysCommon(GUID const& guid, bool deviceClass) {
	ULONG count = 0;
	::CM_Get_Class_Property_Keys(&guid, nullptr, &count, deviceClass ? CM_CLASS_PROPERTY_INSTALLER : CM_CLASS_PROPERTY_INTERFACE);
	if (count == 0)
		return {};

	std::vector<DEVPROPKEY> keys;
	keys.resize(count);
	::CM_Get_Class_Property_Keys(&guid, keys.data(), &count, deviceClass ? CM_CLASS_PROPERTY_INSTALLER : CM_CLASS_PROPERTY_INTERFACE);
	return keys;
}

int DeviceManager::GetDeviceIndex(DEVINST inst) const {
	if (auto it = _devMap.find(inst); it != _devMap.end())
		return it->second;
	return -1;
}

std::unique_ptr<BYTE[]> DeviceManager::GetClassPropertyValue(GUID const& guid, DEVPROPKEY const& key, DEVPROPTYPE& type, ULONG* len) {
	ULONG size = 0;
	::CM_Get_Class_Property(&guid, &key, &type, nullptr, &size, CM_CLASS_PROPERTY_INSTALLER);
	auto value = std::make_unique<BYTE[]>(size);
	::CM_Get_Class_Property(&guid, &key, &type, value.get(), &size, CM_CLASS_PROPERTY_INSTALLER);
	if (len)
		*len = size;
	return value;
}
