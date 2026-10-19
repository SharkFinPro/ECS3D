using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using ScriptBridge;
using Xunit;

namespace ECS3DManagedTests;

// Compiles every shipped script through ScriptCompiler, the same step Bridge uses at runtime, so a
// binding rename that breaks a shipped script fails here instead of at the developer's first run.
public class ShippedScriptsCompileTests : IDisposable
{
  private const string BuiltIn = "built-in";

  private readonly string _dir = Path.Combine(Path.GetTempPath(), "ecs3d-compile-" + Guid.NewGuid().ToString("N"));

  public void Dispose()
  {
    if (Directory.Exists(_dir))
    {
      Directory.Delete(_dir, recursive: true);
    }
  }

  private static string SourceRoot() =>
    typeof(ShippedScriptsCompileTests).Assembly
      .GetCustomAttributes<AssemblyMetadataAttribute>()
      .Single(a => a.Key == "ECS3DSourceRoot").Value!;

  private static string[] CsFiles(string directory) =>
    Directory.Exists(directory)
      ? Directory.GetFiles(directory, "*.cs", SearchOption.AllDirectories).OrderBy(f => f, StringComparer.Ordinal).ToArray()
      : Array.Empty<string>();

  private static string[] BuiltInFiles() => CsFiles(Path.Combine(SourceRoot(), "libs", "scripting", "UserScripts"));

  private static string[] TemplateDirs()
  {
    var templates = Path.Combine(SourceRoot(), "templates");
    if (!Directory.Exists(templates))
    {
      return Array.Empty<string>();
    }

    return Directory.GetDirectories(templates)
      .Where(d => Directory.Exists(Path.Combine(d, "scripts")))
      .OrderBy(d => d, StringComparer.Ordinal)
      .ToArray();
  }

  public static IEnumerable<object[]> ScriptSets()
  {
    yield return new object[] { BuiltIn };
    foreach (var dir in TemplateDirs())
    {
      yield return new object[] { Path.GetFileName(dir) };
    }
  }

  private static string[] FilesForSet(string set) =>
    set == BuiltIn
      ? BuiltInFiles()
      : BuiltInFiles().Concat(CsFiles(Path.Combine(SourceRoot(), "templates", set, "scripts"))).ToArray();

  private string Write(string fileName, string source)
  {
    Directory.CreateDirectory(_dir);
    var path = Path.Combine(_dir, fileName);
    File.WriteAllText(path, source);
    return path;
  }

  private static string Describe(ScriptCompileResult result) =>
    string.Join(Environment.NewLine,
      result.DuplicateMessages.Concat(result.Errors.Select(e => e.ToString())));

  private static List<string> UnsupportedFields(byte[] assembly)
  {
    using var ms = new MemoryStream(assembly);
    var context = new ScriptContext(ms);
    try
    {
      var asm = context.ScriptTypes[0].Assembly;
      return ScriptCompiler.UnsupportedExposedFields(asm)
        .Select(u => $"{u.scriptType.Name}.{u.field.Name}: {u.field.FieldType.Name}")
        .ToList();
    }
    finally
    {
      context.Unload();
    }
  }

  [Fact]
  public void BuiltInSet_IsNotVacuous()
  {
    var files = BuiltInFiles();

    Assert.NotEmpty(files);
    Assert.Contains(files, f => Path.GetFileName(f) == "PlayerScript.cs");
  }

  [Theory]
  [MemberData(nameof(ScriptSets))]
  public void ShippedScripts_CompileWithSupportedExposedFields(string set)
  {
    var result = ScriptCompiler.Compile(FilesForSet(set));

    Assert.True(result.Success, $"Set '{set}' failed to compile:{Environment.NewLine}{Describe(result)}");
    Assert.Empty(result.DuplicateMessages);
    Assert.Empty(UnsupportedFields(result.Assembly!));
  }

  [Fact]
  public void SyntaxOrTypeError_FailsAndIsReported()
  {
    var good = Write("Good.cs", "using ScriptBridge;\npublic class Good : ScriptBase { }\n");
    var bad = Write("Bad.cs", "using ScriptBridge;\npublic class Bad : ScriptBase { int x = \"text\"; }\n");

    Assert.True(ScriptCompiler.Compile(new[] { good }).Success);

    var result = ScriptCompiler.Compile(new[] { good, bad });

    Assert.False(result.Success);
    Assert.Null(result.Assembly);
    Assert.Contains(result.Errors, e => e.ToString().Contains("Bad.cs"));
  }

  [Fact]
  public void SameClassInTwoFiles_FailsNamingBothFiles()
  {
    var a = Write("A.cs", "using ScriptBridge;\npublic class Dup : ScriptBase { }\n");
    var b = Write("B.cs", "using ScriptBridge;\npublic class Dup : ScriptBase { }\n");

    var result = ScriptCompiler.Compile(new[] { a, b });

    Assert.False(result.Success);
    var message = Assert.Single(result.DuplicateMessages);
    Assert.Contains("'Dup'", message);
    Assert.Contains(a, message);
    Assert.Contains(b, message);
  }

  [Fact]
  public void PartialClassAcrossFiles_Compiles()
  {
    var a = Write("A.cs", "using ScriptBridge;\npublic partial class Split : ScriptBase { }\n");
    var b = Write("B.cs", "public partial class Split { public int Value = 1; }\n");

    var result = ScriptCompiler.Compile(new[] { a, b });

    Assert.True(result.Success, Describe(result));
    Assert.Empty(result.DuplicateMessages);
  }

  [Fact]
  public void ExposedFieldTypes_UnsupportedIsReportedAndSupportedIsNot()
  {
    var supported = Write("Supported.cs",
      "using System.Numerics;\nusing ScriptBridge;\n" +
      "public class Supported : ScriptBase\n{\n" +
      "  [ExposeToEditor(\"F\")] private float m_f;\n" +
      "  [ExposeToEditor(\"I\")] private int m_i;\n" +
      "  [ExposeToEditor(\"B\")] private bool m_b;\n" +
      "  [ExposeToEditor(\"S\")] private string m_s = \"\";\n" +
      "  [ExposeToEditor(\"V\")] private Vector3 m_v;\n" +
      "}\n");
    var unsupported = Write("Unsupported.cs",
      "using ScriptBridge;\npublic class Unsupported : ScriptBase\n{\n" +
      "  [ExposeToEditor(\"X\")] private double m_x;\n}\n");

    var okResult = ScriptCompiler.Compile(new[] { supported });
    Assert.True(okResult.Success, Describe(okResult));
    Assert.Empty(UnsupportedFields(okResult.Assembly!));

    var badResult = ScriptCompiler.Compile(new[] { supported, unsupported });
    Assert.True(badResult.Success, Describe(badResult));
    var fields = UnsupportedFields(badResult.Assembly!);
    Assert.Equal(new[] { "Unsupported.m_x: Double" }, fields);
  }
}
