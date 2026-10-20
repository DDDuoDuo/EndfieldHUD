import Foundation
import CommonCrypto
import Security
import LocalAuthentication

/// What the archive knows about one document's classification. The text of a
/// classified document stays in the archive; the UI covers it with redaction
/// bars until its password is entered in the current visit.
enum ArchiveClassificationStatus: Equatable {
    case unclassified
    /// Classified with a readable password verifier: show the locked view.
    case protected
    /// Classified, but this Mac's Keychain holds no usable verifier (a restored
    /// or copied archive, or a removed or damaged item). The document stays
    /// classified and locked; only owner-authenticated recovery can reopen it.
    case passwordMissing
    /// Classified; a verifier may exist but cannot be read now (a locked
    /// Keychain, or a build that this item's access list does not trust).
    case keychainUnavailable
}

enum ArchiveUnlockResult: Equatable {
    case unlocked
    case wrongPassword
    /// The Keychain holds no usable password; offer the recovery path.
    case passwordMissing
    /// Not applied: another unlock is running, or the document was left.
    case ignored
    /// The Keychain could not be read. The message is user-visible.
    case failed(String)
}

enum ArchivePasswordIssue: Equatable { case tooShort, tooLong, mismatch }

enum ArchivePassword {
    static let minimumLength = 4
    static let maximumBytes = 1024
    /// Matches the classify pop-over: a short first field is reported before
    /// a mismatched confirmation. Lengths count user-perceived characters.
    static func issue(_ password: String, confirmation: String? = nil) -> ArchivePasswordIssue? {
        if password.count < minimumLength { return .tooShort }
        if password.utf8.count > maximumBytes { return .tooLong }
        if let confirmation, confirmation != password { return .mismatch }
        return nil
    }
    /// Canonical composition, so the same typed password always derives the same key.
    static func bytes(_ password: String) -> [UInt8] { Array(password.precomposedStringWithCanonicalMapping.utf8) }
    static func account(document: UUID, key: UUID) -> String { document.uuidString + "/" + key.uuidString }
}

/// A salted PBKDF2-HMAC-SHA256 verifier. Only this record is stored, and only
/// in the Keychain; the password itself is never written anywhere.
struct ArchivePasswordVerifier: Codable, Equatable {
    static let algorithmName = "PBKDF2-HMAC-SHA256"
    static let saltLength = 16
    static let keyLength = 32
    /// OWASP's floor for PBKDF2-SHA256; about 85 ms on an M2.
    static let minimumIterations: UInt32 = 600_000
    /// Bounds the time a damaged or hostile record can cost one verification.
    static let maximumIterations: UInt32 = 10_000_000
    static let targetMilliseconds: UInt32 = 80
    /// Measured once per process so verification on this Mac takes roughly
    /// the target time, never below the floor.
    static let calibratedIterations: UInt32 = {
        let rounds = CCCalibratePBKDF(CCPBKDFAlgorithm(kCCPBKDF2), 16, saltLength,
            CCPseudoRandomAlgorithm(kCCPRFHmacAlgSHA256), keyLength, targetMilliseconds)
        return min(maximumIterations, max(minimumIterations, rounds))
    }()

    var version = 1
    var algorithm = ArchivePasswordVerifier.algorithmName
    var iterations: UInt32
    var salt: Data
    var key: Data

    var isValid: Bool {
        version == 1 && algorithm == Self.algorithmName && (Self.minimumIterations...Self.maximumIterations).contains(iterations)
            && salt.count == Self.saltLength && key.count == Self.keyLength
    }
    static func make(password: String, iterations: UInt32 = calibratedIterations) throws -> ArchivePasswordVerifier {
        guard ArchivePassword.issue(password) == nil, (minimumIterations...maximumIterations).contains(iterations) else { throw ArchiveError.invalid }
        var salt = Data(count: saltLength)
        let status = salt.withUnsafeMutableBytes { SecRandomCopyBytes(kSecRandomDefault, saltLength, $0.baseAddress!) }
        guard status == errSecSuccess, let key = derive(password, salt: salt, iterations: iterations) else { throw ArchiveError.unavailable }
        return ArchivePasswordVerifier(iterations: iterations, salt: salt, key: key)
    }
    /// Always runs the full derivation, then compares in constant time.
    func matches(_ password: String) -> Bool {
        guard isValid, password.utf8.count <= ArchivePassword.maximumBytes,
              let candidate = Self.derive(password, salt: salt, iterations: iterations) else { return false }
        return Self.constantTimeEqual(candidate, key)
    }
    static func derive(_ password: String, salt: Data, iterations: UInt32) -> Data? {
        var bytes = ArchivePassword.bytes(password)
        defer { bytes.withUnsafeMutableBytes { if let base = $0.baseAddress { memset_s(base, $0.count, 0, $0.count) } } }
        var key = Data(count: keyLength)
        let status = key.withUnsafeMutableBytes { output in
            salt.withUnsafeBytes { saltBytes in
                bytes.withUnsafeBytes { passwordBytes in
                    CCKeyDerivationPBKDF(CCPBKDFAlgorithm(kCCPBKDF2), passwordBytes.baseAddress?.assumingMemoryBound(to: CChar.self), passwordBytes.count,
                        saltBytes.baseAddress?.assumingMemoryBound(to: UInt8.self), saltBytes.count,
                        CCPseudoRandomAlgorithm(kCCPRFHmacAlgSHA256), iterations,
                        output.baseAddress?.assumingMemoryBound(to: UInt8.self), keyLength)
                }
            }
        }
        return status == kCCSuccess ? key : nil
    }
    static func constantTimeEqual(_ a: Data, _ b: Data) -> Bool {
        guard a.count == b.count else { return false }
        var difference: UInt8 = 0
        a.withUnsafeBytes { (left: UnsafeRawBufferPointer) in b.withUnsafeBytes { (right: UnsafeRawBufferPointer) in
            for index in 0..<left.count { difference |= left[index] ^ right[index] }
        } }
        return difference == 0
    }
    func encoded() throws -> Data { let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]; return try encoder.encode(self) }
    init(iterations: UInt32, salt: Data, key: Data) { self.iterations = iterations; self.salt = salt; self.key = key }
    /// Nil for anything that is not a complete, current verifier record.
    init?(data: Data) {
        guard data.count <= 4096, let value = try? JSONDecoder().decode(ArchivePasswordVerifier.self, from: data), value.isValid else { return nil }
        self = value
    }
}

struct ArchiveKeychainError: Error, Equatable { let status: OSStatus }

/// Holds one verifier record per classification. Accounts are
/// "<document UUID>/<key UUID>"; the archive row stores only the key UUID, so
/// a replacement never overwrites or depends on an older item.
protocol ArchivePasswordVault: AnyObject {
    /// Nil when no item exists. Throws when an item may exist but cannot be read.
    func read(account: String) throws -> Data?
    func add(_ data: Data, account: String) throws
    /// Succeeds when the item is already absent.
    func remove(account: String) throws
    func accounts() throws -> [String]
}

final class ArchiveKeychainVault: ArchivePasswordVault {
    static let defaultService = "com.ddduoduo.EndfieldHUD.archive.classified"
    let service: String
    init(service: String = ArchiveKeychainVault.defaultService) { self.service = service }

    func read(account: String) throws -> Data? {
        var query = identity(account)
        query[kSecReturnData as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitOne
        query[kSecUseAuthenticationContext as String] = Self.silentContext()
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        if status == errSecItemNotFound { return nil }
        guard status == errSecSuccess else { throw ArchiveKeychainError(status: status) }
        guard let data = result as? Data else { throw ArchiveKeychainError(status: errSecDecode) }
        return data
    }
    func add(_ data: Data, account: String) throws {
        var attributes = identity(account)
        attributes[kSecValueData as String] = data
        attributes[kSecAttrAccessible as String] = kSecAttrAccessibleWhenUnlockedThisDeviceOnly
        attributes[kSecAttrLabel as String] = "EndfieldHUD Archive"
        attributes[kSecAttrDescription as String] = "Classified document password verifier"
        let status = SecItemAdd(attributes as CFDictionary, nil)
        guard status == errSecSuccess else { throw ArchiveKeychainError(status: status) }
    }
    func remove(account: String) throws {
        let status = SecItemDelete(identity(account) as CFDictionary)
        guard status == errSecSuccess || status == errSecItemNotFound else { throw ArchiveKeychainError(status: status) }
    }
    func accounts() throws -> [String] {
        var query = identity(nil)
        query[kSecReturnAttributes as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitAll
        query[kSecUseAuthenticationContext as String] = Self.silentContext()
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        if status == errSecItemNotFound { return [] }
        guard status == errSecSuccess else { throw ArchiveKeychainError(status: status) }
        return (result as? [[String: Any]] ?? []).compactMap { $0[kSecAttrAccount as String] as? String }
    }
    /// The archive worker never waits on a Keychain dialog; a denied or locked
    /// read is reported as unavailable instead.
    private static func silentContext() -> LAContext { let context = LAContext(); context.interactionNotAllowed = true; return context }
    private func identity(_ account: String?) -> [String: Any] {
        var query: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service,
                                    kSecAttrSynchronizable as String: false]
        if let account { query[kSecAttrAccount as String] = account }
        return query
    }
}

/// Diagnostic processes and fixtures use this instead of the Keychain.
final class ArchiveMemoryPasswordVault: ArchivePasswordVault {
    private let lock = NSLock()
    private var items: [String: Data] = [:]
    func read(account: String) throws -> Data? { lock.lock(); defer { lock.unlock() }; return items[account] }
    func add(_ data: Data, account: String) throws {
        lock.lock(); defer { lock.unlock() }
        guard items[account] == nil else { throw ArchiveKeychainError(status: errSecDuplicateItem) }
        items[account] = data
    }
    func remove(account: String) throws { lock.lock(); defer { lock.unlock() }; items.removeValue(forKey: account) }
    func accounts() throws -> [String] { lock.lock(); defer { lock.unlock() }; return Array(items.keys) }
}

/// Gate for the missing-password recovery path: proves the Mac's owner is
/// present (Touch ID, Apple Watch or the login password).
protocol ArchiveOwnerAuthenticating: AnyObject {
    func authenticateOwner(reason: String, completion: @escaping (Bool) -> Void)
}

final class ArchiveDeviceOwnerAuthentication: ArchiveOwnerAuthenticating {
    func authenticateOwner(reason: String, completion: @escaping (Bool) -> Void) {
        let context = LAContext()
        var error: NSError?
        guard context.canEvaluatePolicy(.deviceOwnerAuthentication, error: &error) else {
            // A Mac account without a password has no stronger owner proof to ask for.
            completion(error?.code == LAError.passcodeNotSet.rawValue); return
        }
        context.evaluatePolicy(.deviceOwnerAuthentication, localizedReason: reason) { approved, _ in completion(approved) }
    }
}
