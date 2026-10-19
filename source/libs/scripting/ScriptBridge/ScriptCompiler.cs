using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace ScriptBridge;

internal sealed class ScriptCompileResult
{
    public bool Success { get; init; }
    public IReadOnlyList<Diagnostic> Errors { get; init; } = Array.Empty<Diagnostic>();
    public IReadOnlyList<Diagnostic> Warnings { get; init; } = Array.Empty<Diagnostic>();
    public IReadOnlyList<string> DuplicateMessages { get; init; } = Array.Empty<string>();
    public byte[]? Assembly { get; init; }
}

internal static class ScriptCompiler
{
    internal static string ReadFileSafe(string path)
    {
        using var fs = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.ReadWrite | FileShare.Delete
        );

        using var reader = new StreamReader(fs);
        return reader.ReadToEnd();
    }

    internal static ScriptCompileResult Compile(IReadOnlyList<string> sourceFiles)
    {
        var syntaxTrees = sourceFiles
            .Select(path => CSharpSyntaxTree.ParseText(ReadFileSafe(path), path: path))
            .ToArray();

        var duplicates = FindDuplicateClasses(syntaxTrees);

        var compilation = CSharpCompilation.Create(
            assemblyName: "UserScripts_" + Guid.NewGuid().ToString("N")[..8],
            syntaxTrees: syntaxTrees,
            references: Directory.GetFiles(Path.GetDirectoryName(typeof(object).Assembly.Location)!, "*.dll")
                            .Where(dll => {
                                try { System.Reflection.AssemblyName.GetAssemblyName(dll); return true; }
                                catch { return false; }
                            })
                            .Select(dll => (MetadataReference)MetadataReference.CreateFromFile(dll))
                            .Append(MetadataReference.CreateFromFile(typeof(ScriptBase).Assembly.Location))
                            .ToArray(),
            options: new CSharpCompilationOptions(
                OutputKind.DynamicallyLinkedLibrary,
                optimizationLevel: OptimizationLevel.Debug,
                nullableContextOptions: NullableContextOptions.Enable));

        using var ms = new MemoryStream();
        var emit = compilation.Emit(ms);

        return new ScriptCompileResult
        {
            Success = emit.Success && duplicates.Count == 0,
            Errors = emit.Diagnostics.Where(d => d.Severity == DiagnosticSeverity.Error).ToList(),
            Warnings = emit.Diagnostics.Where(d => d.Severity == DiagnosticSeverity.Warning).ToList(),
            DuplicateMessages = duplicates,
            Assembly = emit.Success ? ms.ToArray() : null
        };
    }

    // Scripts are looked up by class name, so the same name in two files is ambiguous. Partial
    // declarations of one class are fine, however many files they span.
    private static List<string> FindDuplicateClasses(IEnumerable<SyntaxTree> trees)
    {
        var declarations = trees
            .SelectMany(tree => tree.GetRoot().DescendantNodes().OfType<ClassDeclarationSyntax>()
                .Where(c => c.Parent is not TypeDeclarationSyntax)
                .Select(c => (name: c.Identifier.Text, file: tree.FilePath,
                              isPartial: c.Modifiers.Any(m => m.IsKind(SyntaxKind.PartialKeyword)))));

        var messages = new List<string>();
        foreach (var group in declarations.GroupBy(d => d.name).OrderBy(g => g.Key, StringComparer.Ordinal))
        {
            var files = group.Select(d => d.file).Distinct().ToList();
            if (files.Count < 2 || group.All(d => d.isPartial))
            {
                continue;
            }

            var joined = files.Count == 2
                ? $"{files[0]} and {files[1]}"
                : string.Join(", ", files.Take(files.Count - 1)) + " and " + files[^1];
            messages.Add($"Duplicate script class '{group.Key}' in {joined}");
        }

        return messages;
    }

    // [ExposeToEditor] fields on script types whose type the editor cannot show; Bridge silently skips them.
    internal static IEnumerable<(Type scriptType, FieldInfo field)> UnsupportedExposedFields(Assembly assembly)
    {
        foreach (var type in assembly.GetTypes().Where(t => t.IsSubclassOf(typeof(ScriptInstance))))
        {
            var fields = type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic |
                                        BindingFlags.DeclaredOnly);
            foreach (var field in fields)
            {
                if (field.GetCustomAttribute<ExposeToEditorAttribute>() != null &&
                    Bridge.MapTypeName(field.FieldType) == null)
                {
                    yield return (type, field);
                }
            }
        }
    }
}
