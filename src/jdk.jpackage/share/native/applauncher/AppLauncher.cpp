/*
 * Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include <algorithm>
#include "AppLauncher.h"
#include "JvmLauncher.h"
#include "CfgFile.h"
#include "Log.h"
#include "Dll.h"
#include "Toolbox.h"
#include "SysInfo.h"
#include "FileUtils.h"


AppLauncher::AppLauncher() {
    setInitJvmFromCmdlineOnly(false);
    launcherPath = SysInfo::getProcessModulePath();
    args = SysInfo::getCommandArgs();
}


namespace {

struct find_jvmlib {
    find_jvmlib(const tstring& v): runtimePath(v) {
    }

    bool operator () (const tstring& jvmLibName) const {
        const tstring path = FileUtils::mkpath() << runtimePath << jvmLibName;
        return FileUtils::isFileExists(path);
    }

private:
    const tstring& runtimePath;
};

tstring findJvmLib(const CfgFile& cfgFile, const CfgFile& userCfg,
        const tstring& defaultRuntimePath, const tstring_array& jvmLibNames) {
    const CfgFile::Properties& appOptions = cfgFile.getProperties(
            SectionName::Application);
    const CfgFile::Properties& userAppOptions = userCfg.getProperties(
            SectionName::Application);

    const CfgFile::Properties::const_iterator runtimePathProp = appOptions.find(
            PropertyName::runtime);
    tstring runtimePath;
    if (runtimePathProp != appOptions.end()) {
        // not usually used anymore where the runtime is given in main cfg file
        runtimePath = CfgFile::asString(*runtimePathProp);
        LOG_TRACE(tstrings::any()
                << "Property \"" << PropertyName::runtime.name()
                << "\" found in \"" << SectionName::Application.name()
                << "\" section of launcher config file."
                << " Using Java runtime from \""
                << runtimePath << "\" directory");
    } else if (FileUtils::isFileExists(
            FileUtils::mkpath() << defaultRuntimePath << _T("/lib"))) {
        // if there is a lib subdir in the defaultRuntimePath then use default
        runtimePath = defaultRuntimePath;
        LOG_TRACE(tstrings::any()
                << "Property \"" << PropertyName::runtime.name()
                << "\" not found in \"" << SectionName::Application.name()
                << "\" section of launcher config file."
                << " Using Java runtime from \""
                << runtimePath << "\" directory");
    } else if (FileUtils::isFileExists(
            FileUtils::mkpath() << defaultRuntimePath << _T("/release"))) {
        // no runtime included in package, but there is a release file
        const CfgFile::Properties::const_iterator userRuntimeProp =
                userAppOptions.find(PropertyName::runtime);
        if (userRuntimeProp != userAppOptions.end()) {
            runtimePath = CfgFile::asString(*userRuntimeProp);
            LOG_TRACE(tstrings::any()
                    << "Property \"" << PropertyName::runtime.name()
                    << "\" found in \"" << SectionName::Application.name()
                    << "\" section of user specific launcher config file."
                    << " Using Java runtime from \""
                    << runtimePath << "\" directory");
        } else {
            JP_THROW(tstrings::any() << "No runtime in applicvastion image, "
                    << "No runtime in User level config file. "
                    << "Need to search for runtime matching: \""
                    << defaultRuntimePath << _T("/release") << "\"");
        }
    } else {
        JP_THROW(tstrings::any() << "No runtime in applicvastion image, "
                    << "and no release file given");
    }

    const tstring_array::const_iterator jvmLibNameEntry = std::find_if(
            jvmLibNames.begin(),
            jvmLibNames.end(),
            find_jvmlib(runtimePath));

    if (jvmLibNameEntry == jvmLibNames.end()) {
        JP_THROW(tstrings::any() << "Failed to find JVM in \""
            << runtimePath
            << "\" directory.");
    }

    return FileUtils::mkpath() << runtimePath << *jvmLibNameEntry;
}
} // namespace

Jvm* AppLauncher::createJvmLauncher() const {
    const tstring cfgFilePath = FileUtils::mkpath()
        << appDirPath
        << FileUtils::basename(FileUtils::replaceSuffix(
                launcherPath, _T(".cfg")));

    LOG_TRACE(tstrings::any() << "Launcher config file path: \""
            << cfgFilePath << "\"");

    const tstring appDataPath = SysInfo::getEnvVariable(
            std::nothrow, _T("APPDATA"));

    const tstring userCfgPath = FileUtils::mkpath()
        << appDataPath
        << FileUtils::basename(FileUtils::replaceSuffix(
                launcherPath, _T("")))
        << FileUtils::basename(FileUtils::replaceSuffix(
                launcherPath, _T(".cfg")));

    LOG_TRACE(tstrings::any() << "Urer config file path: " << userCfgPath);

    CfgFile::Macros macros;
    macros[_T("$APPDIR")] = appDirPath;
    macros[_T("$BINDIR")] = FileUtils::dirname(launcherPath);
    macros[_T("$ROOTDIR")] = imageRoot;

    CfgFile cfgFile = CfgFile::load(cfgFilePath).expandMacros(macros);
    // at this point, we will only use the user specific app cfg file
    // for selected elements.
    CfgFile userCfg;
    if (FileUtils::isFileExists(userCfgPath)) {
        userCfg = CfgFile::load(userCfgPath).expandMacros(macros);
    }

    if (!args.empty()) {
        // Override default launcher arguments.
        cfgFile.setPropertyValue(SectionName::ArgOptions,
            PropertyName::arguments, args);
    }

    std::unique_ptr<Jvm> jvm(new Jvm());

    (*jvm)
        .setPath(findJvmLib(cfgFile, userCfg, defaultRuntimePath, jvmLibNames))
        .addArgument(launcherPath);

    if (initJvmFromCmdlineOnly) {
        tstring_array::const_iterator argIt = args.begin();
        const tstring_array::const_iterator argEnd = args.end();
        for (; argIt != argEnd; ++argIt) {
            (*jvm).addArgument(*argIt);
        }
    } else {
        (*jvm).initFromConfigFile(cfgFile);
    }

    return jvm.release();
}


void AppLauncher::launch() const {
    std::unique_ptr<Jvm>(createJvmLauncher())->launch();
}
