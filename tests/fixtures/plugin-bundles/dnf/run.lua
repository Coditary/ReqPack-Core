plugin = {}

local function trim(value)
    return (tostring(value or ""):gsub("^%s+", ""):gsub("%s+$", ""))
end

local function shell_quote(value)
    return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
end

local function split_name_arch(value)
    local name, arch = tostring(value or ""):match("^(.-)%.([^.]+)$")
    if name == nil then
        return trim(value), ""
    end
    return name, arch
end

local function normalize_info_key(key)
    return trim(key):lower():gsub("%s+", " ")
end

local function is_info_section_header(line)
    local trimmed = trim(line)
    return trimmed:match("^%w+ [Pp]ackages$") ~= nil
end

local function parse_dnf_info_sections(stdout)
    local sections = {}
    local current = { header = "", fields = {}, description = {} }

    local function flush_section()
        if next(current.fields) ~= nil or #current.description > 0 then
            table.insert(sections, current)
        end
        current = { header = "", fields = {}, description = {} }
    end

    local in_description = false
    for line in tostring(stdout or ""):gmatch("[^\r\n]+") do
        if is_info_section_header(line) then
            flush_section()
            current.header = trim(line)
            in_description = false
        else
            local key, value = line:match("^%s*([^:]+)%s*:%s*(.*)$")
            if key ~= nil then
                local normalized = normalize_info_key(key)
                if normalized == "description" then
                    in_description = true
                    current.description = {}
                    local first = trim(value)
                    if first ~= "" then
                        table.insert(current.description, first)
                    end
                else
                    in_description = false
                    current.fields[normalized] = trim(value)
                end
            elseif in_description then
                local continuation = line:match("^%s*:%s*(.*)$")
                if continuation ~= nil then
                    local text = trim(continuation)
                    if text ~= "" then
                        table.insert(current.description, text)
                    end
                end
            end
        end
    end
    flush_section()
    return sections
end

local function build_version(fields)
    local version = fields["version"] or ""
    local release = fields["release"] or ""
    if version == "" then
        return "unknown"
    end
    if release ~= "" then
        version = version .. "-" .. release
    end
    local epoch = fields["epoch"]
    if epoch ~= nil and epoch ~= "" and epoch ~= "0" then
        version = epoch .. ":" .. version
    end
    return version
end

local function package_info_from_fields(requestedName, fields, description_lines)
    local description = table.concat(description_lines, "\n")
    if description == "" then
        description = fields["summary"] or "DNF Package"
    end
    return {
        name = fields["name"] or requestedName,
        version = build_version(fields),
        type = "package",
        architecture = fields["architecture"] or "",
        summary = fields["summary"] or "",
        description = description,
        size = fields["download size"] or fields["size"] or "",
        installedSize = fields["installed size"] or "",
        homepage = fields["url"] or "",
        license = fields["license"] or "",
        repository = fields["repository"] or "",
    }
end

local function pick_info_section(sections, requestedName)
    local normalizedRequest = trim(requestedName):lower()
    for _, section in ipairs(sections) do
        local sectionName = trim(section.fields["name"] or ""):lower()
        if sectionName ~= "" and sectionName == normalizedRequest then
            return section
        end
    end
    for _, section in ipairs(sections) do
        if section.header:lower():find("installed", 1, true) then
            return section
        end
    end
    return sections[1]
end

local function parse_dnf_info(requestedName, stdout)
    local sections = parse_dnf_info_sections(stdout)
    if #sections == 0 then
        return nil
    end
    local section = pick_info_section(sections, requestedName)
    if section == nil then
        return nil
    end
    return package_info_from_fields(requestedName, section.fields, section.description)
end

function plugin.getName()
    return "Frozen DNF Fixture"
end

function plugin.getVersion()
    return "1.0.0"
end

function plugin.getSecurityMetadata()
    return {
        purlType = "rpm",
        versionComparatorProfile = "rpm-evr",
    }
end

function plugin.getCategories()
    return { "System", "RPM", "Fedora Native" }
end

plugin.fileExtensions = { ".rpm" }

function plugin.getRequirements()
    return {}
end

function plugin.getMissingPackages(packages)
    local missing = {}
    for _, pkg in ipairs(packages or {}) do
        table.insert(missing, pkg)
    end
    return missing
end

function plugin.install(context, packages)
    return true
end

function plugin.installLocal(context, path)
    return true
end

function plugin.remove(context, packages)
    return true
end

function plugin.update(context, packages)
    return true
end

function plugin.list(context)
    local result = context.exec.run("dnf repoquery --installed --qf '%{name}.%{arch}\\t%{version}-%{release}\\n'")
    local summaryResult = context.exec.run("dnf repoquery --installed --qf '%{name}.%{arch}\\t%{summary}\\n'")
    local summaries = {}
    for line in (summaryResult.stdout or ""):gmatch("[^\r\n]+") do
        local qualifiedName, summary = line:match("^(.-)\t(.+)$")
        if qualifiedName and summary then
            summaries[trim(qualifiedName)] = trim(summary)
        end
    end

    local items = {}
    for line in (result.stdout or ""):gmatch("[^\r\n]+") do
        local qualifiedName, ver = line:match("^(.-)\t(.+)$")
        if qualifiedName and ver then
            local name, arch = split_name_arch(trim(qualifiedName))
            table.insert(items, {
                name = name,
                version = trim(ver),
                type = "package",
                architecture = arch,
                summary = summaries[trim(qualifiedName)] or "DNF package"
            })
        end
    end
    context.events.listed(items)
    return items
end

function plugin.outdated(context)
    local result = context.exec.run("dnf check-update --quiet 2>/dev/null")
    local stdout = result.stdout or ""
    local items = {}
    for line in stdout:gmatch("[^\r\n]+") do
        local qualifiedName, latest = line:match("^(%S+)%s+(%S+)%s")
        if qualifiedName and latest then
            local baseName, arch = split_name_arch(trim(qualifiedName))
            local installed = context.exec.run("rpm -q --qf '%{VERSION}-%{RELEASE}' " .. shell_quote(qualifiedName) .. " 2>/dev/null")
            local installedVersion = trim(installed.stdout or "")
            if installedVersion == "" and qualifiedName ~= baseName then
                local fallback = context.exec.run("rpm -q --qf '%{VERSION}-%{RELEASE}' " .. shell_quote(baseName) .. " 2>/dev/null")
                installedVersion = trim(fallback.stdout or "")
            end
            local summary = context.exec.run("dnf repoquery --installed --qf '%{name}.%{arch}\\t%{summary}\\n'")
            local summaryText = "DNF package"
            for summaryLine in (summary.stdout or ""):gmatch("[^\r\n]+") do
                local summaryName, text = summaryLine:match("^(.-)\t(.+)$")
                if trim(summaryName or "") == qualifiedName then
                    summaryText = trim(text)
                end
            end
            table.insert(items, {
                name = baseName,
                version = installedVersion,
                latestVersion = latest,
                type = "package",
                architecture = arch,
                summary = summaryText,
            })
        end
    end
    context.events.outdated(items)
    return items
end

function plugin.search(context, prompt)
    local result = context.exec.run("dnf search " .. shell_quote(prompt) .. " --quiet")
    local versionResult = context.exec.run("dnf repoquery --qf '%{name}.%{arch}\\t%{version}-%{release}\\n' " .. shell_quote(prompt) .. " 2>/dev/null")
    local versions = {}
    for line in (versionResult.stdout or ""):gmatch("[^\r\n]+") do
        local qualifiedName, version = line:match("^(.-)\t(.+)$")
        if qualifiedName and version then
            versions[trim(qualifiedName)] = trim(version)
        end
    end

    local items = {}
    for line in (result.stdout or ""):gmatch("[^\r\n]+") do
        local name, summary = line:match("^%s*(%S+)%s*\t%s*(.+)$")
        if name ~= nil and not tostring(name):match("^Matched$") and not tostring(name):match("^Last$") then
            local baseName, arch = split_name_arch(name)
            table.insert(items, {
                name = baseName,
                version = versions[trim(name)] or "repo",
                type = "package",
                architecture = arch,
                summary = trim(summary or line)
            })
        end
    end
    context.events.searched(items)
    return items
end

function plugin.info(context, name)
    local result = context.exec.run("dnf info " .. shell_quote(name) .. " --quiet")
    local item = parse_dnf_info(name, result.stdout or "")
    if item == nil then
        item = { name = name, version = "unknown", description = "DNF Package" }
    end
    context.events.informed(item)
    return item
end

function plugin.init()
    return reqpack.exec.run("command -v dnf >/dev/null 2>&1").success
end

function plugin.shutdown()
    return true
end
