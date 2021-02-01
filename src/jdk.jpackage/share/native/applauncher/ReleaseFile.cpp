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

#include "kludge_c++11.h"

#include <fstream>
#include <algorithm>
#include "ReleaseFile.h"
#include "Toolbox.h"
#include "FileUtils.h"
#include "ErrorHandling.h"
#include "Log.h"


namespace {

} // namespace

tstring& ReleaseFile::getVersion() {
    return version;
}

tstring_array& ReleaseFile::getModules() {
    return modules;
}

bool versionAtLeast(tstring& required, tstring& other) {
    tstring_array rvers = tstrings::split(required, _T("."));
    tstring_array overs = tstrings::split(other, _T("."));

    tstring_array::const_iterator rit = rvers.begin();
    const tstring_array::const_iterator rend = rvers.end();

    tstring_array::const_iterator oit = overs.begin();
    const tstring_array::const_iterator oend = overs.end();


    for (; rit != rend; ++rit) {
        int rnum = stoi(*rit);
        const tstring oitstr = (oit != oend) ? *oit++ : _T("0");
        int onum = stoi(oitstr);

        if (rnum < onum) {
            return false;
        }
    }
    return true;
}

bool ReleaseFile::satisfies(ReleaseFile required) {
    // We need to insure version >= required.version
    // for now only equals will work
    if (versionAtLeast(required.version, version)) {
        LOG_TRACE(tstrings::any() << "version: " << version
                << " matches version: " << required.version);
        // now we need to make sure all required modules are there.
        tstring_array reqmods = required.modules;
        tstring_array canmods = modules;
        
        for (int i=0; i<reqmods.size(); i++) {
            int j = 0;
            for (; j<canmods.size(); j++) {
                if (tstrings::equals(reqmods[i], canmods[j])) {
                    break;
                }
            }
            if (j == canmods.size()) {
                LOG_TRACE(tstrings::any() << " missing mod: " << reqmods[i]);
                return false;
            }
        }
        LOG_TRACE(tstrings::any() << " all modules satisfied ");
        return true;
    }
    LOG_TRACE(tstrings::any() << "version: " << version
                << " not matching version: " << required.version);
    return false;
}

ReleaseFile ReleaseFile::load(const tstring& path) {
    std::ifstream input(path.c_str());

    ReleaseFile releaseFile;

    if (input.good()) {
        std::string utf8line;

        int lineno = 0;
        // we should find JAVA_VERSION and MODULES in the first few lines
        while ((std::getline(input, utf8line)) && lineno++ < 10) {
            const tstring line = tstrings::any(utf8line).tstr();
            tstring::size_type spos = 0, epos = 0;
            if (tstrings::startsWith(line, _T("JAVA_VERSION=\""))) {
                spos = line.find(_T("\""), 0) + 1;
                epos = line.find(_T("\""), spos);
                if (spos > 1 && epos > spos) {
                    releaseFile.version = line.substr(spos, epos-spos);
                }
            } else if (tstrings::startsWith(line, _T("MODULES=\""))) {
                spos = line.find(_T("\""), 0) + 1;
                epos = line.find(_T("\""), spos);
                if (spos > 1 && epos > spos) {
                    tstring mods = line.substr(spos, epos - spos);
                    releaseFile.modules = tstrings::split(mods, _T(" "));

                    tstring_array::const_iterator it;
                    for (it = releaseFile.modules.begin();
                         it != releaseFile.modules.end(); ++it) {
                    }
                }
            }
        }
    }

    return releaseFile;
}

