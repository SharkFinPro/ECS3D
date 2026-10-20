#include "TemplateManifest.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <utility>

namespace
{
  constexpr long long maxTemplatePlayers = 64;

  bool isValidId(const std::string& id)
  {
    if (id.empty() || id.front() == '-' || id.back() == '-')
    {
      return false;
    }

    char previous = '\0';
    for (const char c : id)
    {
      const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
      if (!alnum && c != '-')
      {
        return false;
      }

      if (c == '-' && previous == '-')
      {
        return false;
      }

      previous = c;
    }

    return true;
  }

  std::optional<TemplateManifest> fail(std::string* error, const std::string& message)
  {
    if (error)
    {
      *error = message;
    }

    return std::nullopt;
  }

  bool readString(const nlohmann::json& data, const char* key, std::string& out, std::string& problem)
  {
    const auto it = data.find(key);
    if (it == data.end())
    {
      problem = std::string("missing required field '") + key + "'";
      return false;
    }

    if (!it->is_string())
    {
      problem = std::string("field '") + key + "' must be a string";
      return false;
    }

    out = it->get<std::string>();
    return true;
  }
}

std::optional<TemplateManifest> parseTemplateManifest(const nlohmann::json& data, std::string* error)
{
  if (!data.is_object())
  {
    return fail(error, "manifest must be a JSON object");
  }

  TemplateManifest manifest;
  std::string problem;

  if (!readString(data, "id", manifest.id, problem)
      || !readString(data, "name", manifest.name, problem)
      || !readString(data, "description", manifest.description, problem)
      || !readString(data, "category", manifest.category, problem)
      || !readString(data, "thumbnail", manifest.thumbnail, problem)
      || !readString(data, "entryScene", manifest.entryScene, problem)
      || !readString(data, "minEngineVersion", manifest.minEngineVersion, problem))
  {
    return fail(error, problem);
  }

  const std::pair<const char*, const std::string*> nonEmpty[] = {
    { "name", &manifest.name },
    { "description", &manifest.description },
    { "entryScene", &manifest.entryScene },
    { "minEngineVersion", &manifest.minEngineVersion }
  };

  for (const auto& [key, value] : nonEmpty)
  {
    if (value->empty())
    {
      return fail(error, std::string("field '") + key + "' must not be empty");
    }
  }

  if (!isValidId(manifest.id))
  {
    return fail(error, "field 'id' must be lowercase letters and digits separated by single hyphens");
  }

  if (manifest.category != "template" && manifest.category != "sample")
  {
    return fail(error, "field 'category' must be \"template\" or \"sample\"");
  }

  const auto tags = data.find("tags");
  if (tags == data.end())
  {
    return fail(error, "missing required field 'tags'");
  }

  if (!tags->is_array())
  {
    return fail(error, "field 'tags' must be an array of strings");
  }

  for (const auto& tag : *tags)
  {
    if (!tag.is_string())
    {
      return fail(error, "field 'tags' must be an array of strings");
    }

    manifest.tags.push_back(tag.get<std::string>());
  }

  if (const auto players = data.find("players"); players != data.end())
  {
    if (!players->is_object())
    {
      return fail(error, "field 'players' must be an object with 'min' and 'max'");
    }

    const auto min = players->find("min");
    const auto max = players->find("max");
    if (min == players->end() || max == players->end() || !min->is_number_integer()
        || !max->is_number_integer())
    {
      return fail(error, "field 'players' needs integer 'min' and 'max'");
    }

    const auto minValue = min->get<long long>();
    const auto maxValue = max->get<long long>();
    if (minValue < 1 || maxValue < minValue || maxValue > maxTemplatePlayers)
    {
      return fail(error, "field 'players' needs 1 <= min <= max <= 64");
    }

    manifest.players = TemplatePlayers{ static_cast<int>(minValue), static_cast<int>(maxValue) };
  }

  return manifest;
}

std::optional<TemplateManifest> loadTemplateManifest(const std::filesystem::path& folder, std::string* error)
{
  try
  {
    const auto path = folder / templateManifestFileName;

    std::ifstream file(path);
    if (!file.is_open())
    {
      return fail(error, "could not open " + path.string());
    }

    const auto data = nlohmann::json::parse(file);
    return parseTemplateManifest(data, error);
  }
  catch (const std::exception& e)
  {
    return fail(error, std::string("could not read manifest: ") + e.what());
  }
}
