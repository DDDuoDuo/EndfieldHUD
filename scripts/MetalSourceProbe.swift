import Foundation
import CryptoKit

// Compile translated source programs without creating a Metal device or
// executing game or shader code. This works on an Xcode host without a GPU.
struct StageResult: Codable {
    var file: String
    var sha256: String
    var exitCode: Int32
    var diagnosticFile: String
}

struct ProbeReport: Codable {
    var schemaVersion = 1
    var resourceRoot: String
    var stageCount: Int
    var sourceMetalCompilationPassed: Bool
    var nativeDeviceRenderingVerified = false
    var fullSPIRVValidationPerformed = false
    var stages: [StageResult]
}

enum ProbeFailure: Error {
    case invalid(String)
}

func dictionary(_ object: Any?) throws -> [String: Any] {
    guard let value = object as? [String: Any] else { throw ProbeFailure.invalid("Expected source interface object") }
    return value
}

func number(_ object: Any?) throws -> Int {
    guard let value = object as? NSNumber else { throw ProbeFailure.invalid("Expected source interface integer") }
    return value.intValue
}

func runProbe() throws -> Bool {
    guard CommandLine.arguments.count == 3 else {
        throw ProbeFailure.invalid("Usage: MetalSourceProbe resource-root output-directory")
    }
    let files = FileManager.default
    let resource = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true).standardizedFileURL
    let output = URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true).standardizedFileURL
    try files.createDirectory(at: output, withIntermediateDirectories: true)
    let shaders = resource.appendingPathComponent("Shaders", isDirectory: true)
    let metalFiles = try files.contentsOfDirectory(at: shaders, includingPropertiesForKeys: nil)
        .filter { $0.pathExtension == "metal" }.sorted { $0.lastPathComponent < $1.lastPathComponent }
    guard !metalFiles.isEmpty else { throw ProbeFailure.invalid("No translated source Metal programs") }

    // Validate the actual runtime JSON against its compiled stage's binding
    // declarations. The exporter independently verifies source byte offsets.
    let interfaces = try files.contentsOfDirectory(at: resource, includingPropertiesForKeys: nil)
        .filter { $0.lastPathComponent.hasSuffix("-shader.json") }
    for interface in interfaces {
        let object = try dictionary(JSONSerialization.jsonObject(with: Data(contentsOf: interface)))
        let stages = try dictionary(object["stages"])
        for (stageName, stageObject) in stages {
            let stage = try dictionary(stageObject)
            guard let file = stage["file"] as? String else { throw ProbeFailure.invalid("Missing stage file") }
            let url = resource.appendingPathComponent(file).standardizedFileURL
            guard url.path.hasPrefix(shaders.path + "/"), metalFiles.contains(url) else {
                throw ProbeFailure.invalid("Stage path escapes source shader directory: \(file)")
            }
            let source = try String(contentsOf: url, encoding: .utf8)
            for uniformObject in stage["uniforms"] as? [[String: Any]] ?? [] {
                let index = try number(uniformObject["index"])
                let size = try number(uniformObject["size"])
                guard size > 0, source.contains("[[buffer(\(index))]]") else {
                    throw ProbeFailure.invalid("Runtime constant buffer not declared in \(file)")
                }
                for field in uniformObject["fields"] as? [[String: Any]] ?? [] {
                    let offset = try number(field["offset"])
                    guard offset >= 0, offset < size else { throw ProbeFailure.invalid("Constant field exceeds source buffer") }
                }
            }
            for texture in object["textures"] as? [[String: Any]] ?? [] {
                guard (texture["stage"] as? String ?? "fragment") == stageName else { continue }
                let index = try number(texture["index"])
                let sampler = try number(texture["sampler_index"] ?? texture["index"])
                guard source.contains("[[texture(\(index))]]"), source.contains("[[sampler(\(sampler))]]") else {
                    throw ProbeFailure.invalid("Runtime texture/sampler not declared in \(file)")
                }
            }
        }
    }

    var results: [StageResult] = []
    for source in metalFiles {
        let stem = source.deletingPathExtension().lastPathComponent
        let log = output.appendingPathComponent(stem + ".log")
        guard files.createFile(atPath: log.path, contents: nil) else { throw ProbeFailure.invalid("Cannot create Metal compiler log") }
        let handle = try FileHandle(forWritingTo: log)
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/xcrun")
        process.arguments = ["-sdk", "macosx", "metal", "-std=metal2.3", "-c", source.path,
                             "-o", output.appendingPathComponent(stem + ".air").path]
        process.standardOutput = handle
        process.standardError = handle
        do {
            try process.run()
            process.waitUntilExit()
            try handle.close()
        } catch {
            try? handle.close()
            throw error
        }
        let sha = SHA256.hash(data: try Data(contentsOf: source)).map { String(format: "%02x", $0) }.joined()
        results.append(StageResult(file: "Shaders/" + source.lastPathComponent, sha256: sha,
                                   exitCode: process.terminationStatus, diagnosticFile: log.lastPathComponent))
        print("\(process.terminationStatus == 0 ? "PASS" : "FAIL") \(source.lastPathComponent)")
    }
    let passed = results.allSatisfy { $0.exitCode == 0 }
    let report = ProbeReport(resourceRoot: resource.path, stageCount: results.count,
                             sourceMetalCompilationPassed: passed, stages: results)
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
    try encoder.encode(report).write(to: output.appendingPathComponent("report.json"))
    print("Metal source compilation: \(results.filter { $0.exitCode == 0 }.count)/\(results.count); device rendering remains unverified")
    return passed
}

do {
    exit(try runProbe() ? 0 : 1)
} catch {
    fputs("Metal source probe failed: \(error)\n", stderr)
    exit(1)
}
