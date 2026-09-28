import Foundation
import CryptoKit

struct VerificationError: Error, CustomStringConvertible {
    let description: String
    init(_ text: String) { description = text }
}
func verify(_ data: Data, signature: String, key: Data) throws {
    guard let signature = Data(base64Encoded: signature), signature.count == 64 else {
        throw VerificationError("Invalid Ed25519 signature format")
    }
    let publicKey = try Curve25519.Signing.PublicKey(rawRepresentation: key)
    guard publicKey.isValidSignature(signature, for: data) else {
        throw VerificationError("Ed25519 signature verification failed")
    }
}
func verifyFeed(_ data: Data, key: Data) throws {
    let prefix = Data("<!-- sparkle-signatures:\n".utf8)
    guard let range = data.range(of: prefix, options: .backwards),
          let suffix = data.range(of: Data("-->".utf8), in: range.upperBound..<data.endIndex),
          data[suffix.upperBound...].allSatisfy({ [9, 10, 13, 32].contains($0) }),
          let block = String(data: data[range.upperBound..<suffix.lowerBound], encoding: .utf8) else {
        throw VerificationError("Missing or malformed Sparkle feed signature block")
    }
    var fields: [String: String] = [:]
    for line in block.split(separator: "\n") {
        let pair = line.split(separator: ":", maxSplits: 1)
        guard pair.count == 2, fields[String(pair[0])] == nil else {
            throw VerificationError("Malformed feed signature fields")
        }
        fields[String(pair[0])] = pair[1].trimmingCharacters(in: .whitespaces)
    }
    let content = data[..<range.lowerBound]
    guard let signature = fields["edSignature"], let length = fields["length"],
          Int(length) == content.count else { throw VerificationError("Feed signature length mismatch") }
    try verify(Data(content), signature: signature, key: key)
}
func selfTest() throws {
    let privateKey = Curve25519.Signing.PrivateKey()
    let publicKey = privateKey.publicKey.rawRepresentation
    let content = Data("<?xml version=\"1.0\"?><rss><channel/></rss>\n".utf8)
    let signature = try privateKey.signature(for: content).base64EncodedString()
    let feed = content + Data("<!-- sparkle-signatures:\nedSignature: \(signature)\nlength: \(content.count)\n-->\n".utf8)
    try verify(content, signature: signature, key: publicKey)
    try verifyFeed(feed, key: publicKey)
    let rejected: [() throws -> Void] = [
        { try verify(content + Data([0]), signature: signature, key: publicKey) },
        { try verify(content, signature: signature, key: Curve25519.Signing.PrivateKey().publicKey.rawRepresentation) },
        { try verify(content, signature: "bad", key: publicKey) },
        { try verifyFeed(content, key: publicKey) },
        { try verifyFeed(feed + Data("garbage".utf8), key: publicKey) },
        { try verifyFeed(Data(String(decoding: feed, as: UTF8.self).replacingOccurrences(of: "length: \(content.count)", with: "length: 1").utf8), key: publicKey) },
    ]
    for rejection in rejected {
        var failed = false
        do { try rejection() } catch { failed = true }
        guard failed else { throw VerificationError("Tampered update fixture was accepted") }
    }
    print("PASS: archive/feed signatures; modified content, wrong keys and malformed feeds rejected")
}
do {
    let args = Array(CommandLine.arguments.dropFirst())
    if args == ["--self-test"] { try selfTest() }
    else {
        guard args.count >= 3 else { throw VerificationError("Usage: verify-update-signature INFO.plist --feed APPCAST | INFO.plist --archive ZIP SIGNATURE") }
        let info = try PropertyListSerialization.propertyList(from: Data(contentsOf: URL(fileURLWithPath: args[0])), format: nil) as? [String: Any]
        guard let encoded = info?["SUPublicEDKey"] as? String, let key = Data(base64Encoded: encoded), key.count == 32 else {
            throw VerificationError("Info.plist has no valid update public key")
        }
        let data = try Data(contentsOf: URL(fileURLWithPath: args[2]), options: .mappedIfSafe)
        if args[1] == "--feed", args.count == 3 { try verifyFeed(data, key: key) }
        else if args[1] == "--archive", args.count == 4 { try verify(data, signature: args[3], key: key) }
        else { throw VerificationError("Invalid signature verification arguments") }
        print("Verified update signature")
    }
} catch {
    FileHandle.standardError.write(Data("\(error)\n".utf8))
    exit(1)
}
