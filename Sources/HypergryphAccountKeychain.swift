import Foundation
import Security
import LocalAuthentication

/// Stores community-scoped credentials only. Passport tokens, cookies, phone
/// numbers and passwords never enter this vault or the profile data file.
protocol HypergryphAccountCredentialVault: AnyObject {
    func load(region: HypergryphAccountRegion) throws -> HypergryphCredentials?
    func save(_ credentials: HypergryphCredentials, region: HypergryphAccountRegion) throws
    func remove(region: HypergryphAccountRegion) throws
}

struct HypergryphKeychainError: Error, Equatable {
    let status: OSStatus
}

protocol HypergryphKeychainAccess {
    func read(_ query: [String: Any]) -> (OSStatus, Data?)
    func update(_ query: [String: Any], attributes: [String: Any]) -> OSStatus
    func add(_ attributes: [String: Any]) -> OSStatus
    func delete(_ query: [String: Any]) -> OSStatus
}

private struct HypergryphSystemKeychainAccess: HypergryphKeychainAccess {
    func read(_ query: [String: Any]) -> (OSStatus, Data?) {
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        return (status, result as? Data)
    }
    func update(_ query: [String: Any], attributes: [String: Any]) -> OSStatus {
        SecItemUpdate(query as CFDictionary, attributes as CFDictionary)
    }
    func add(_ attributes: [String: Any]) -> OSStatus { SecItemAdd(attributes as CFDictionary, nil) }
    func delete(_ query: [String: Any]) -> OSStatus { SecItemDelete(query as CFDictionary) }
}

final class HypergryphAccountKeychain: HypergryphAccountCredentialVault {
    private let service: String
    private let access: HypergryphKeychainAccess
    init(service: String = "com.ddduoduo.EndfieldHUD.hypergryph.account", access: HypergryphKeychainAccess? = nil) {
        self.service = service; self.access = access ?? HypergryphSystemKeychainAccess()
    }

    func load(region: HypergryphAccountRegion) throws -> HypergryphCredentials? {
        var query = identity(region)
        query[kSecReturnData as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitOne
        let context = LAContext(); context.interactionNotAllowed = true
        query[kSecUseAuthenticationContext as String] = context
        let (status, result) = access.read(query)
        if status == errSecItemNotFound { return nil }
        guard status == errSecSuccess else { throw HypergryphKeychainError(status: status) }
        guard let data = result,
              let credentials = try? JSONDecoder().decode(HypergryphCredentials.self, from: data) else {
            throw HypergryphKeychainError(status: errSecDecode)
        }
        return credentials
    }

    func save(_ credentials: HypergryphCredentials, region: HypergryphAccountRegion) throws {
        let data = try JSONEncoder().encode(credentials)
        var attributes: [String: Any] = [kSecValueData as String: data,
            kSecAttrAccessible as String: kSecAttrAccessibleWhenUnlockedThisDeviceOnly]
        let status = access.update(identity(region), attributes: attributes)
        if status == errSecSuccess { return }
        guard status == errSecItemNotFound else { throw HypergryphKeychainError(status: status) }
        attributes.merge(identity(region)) { _, identity in identity }
        let added = access.add(attributes)
        guard added == errSecSuccess else { throw HypergryphKeychainError(status: added) }
    }

    func remove(region: HypergryphAccountRegion) throws {
        let status = access.delete(identity(region))
        guard status == errSecSuccess || status == errSecItemNotFound else {
            throw HypergryphKeychainError(status: status)
        }
    }

    private func identity(_ region: HypergryphAccountRegion) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword,
         kSecAttrService as String: service,
         kSecAttrAccount as String: region.rawValue,
         kSecAttrSynchronizable as String: false]
    }
}

/// Fixtures and ephemeral previews inject this instead of accessing Keychain.
final class HypergryphMemoryCredentialVault: HypergryphAccountCredentialVault {
    private var credentials: [HypergryphAccountRegion: HypergryphCredentials] = [:]
    func load(region: HypergryphAccountRegion) throws -> HypergryphCredentials? { credentials[region] }
    func save(_ value: HypergryphCredentials, region: HypergryphAccountRegion) throws { credentials[region] = value }
    func remove(region: HypergryphAccountRegion) throws { credentials.removeValue(forKey: region) }
}
