/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The Initial Developer of the Original Code and related documentation
 * is America Online, Inc. Portions created by AOL are Copyright (C) 1999
 * America Online, Inc. All Rights Reserved.
 *
 */

/*
 * nsversion.h --
 *
 *      Version info maintained in configure.in
 *
 */

#ifndef NSVERSION_H
#define NSVERSION_H


#define NS_MAJOR_VERSION    5
#define NS_MINOR_VERSION    0
#define NS_RELEASE_SERIAL   4
#define NS_VERSION_NUM      (NS_MAJOR_VERSION * 10000 \
                             + NS_MINOR_VERSION * 100 \
                             + NS_RELEASE_SERIAL)

#define NS_VERSION          "5.0"
#define NS_PATCH_LEVEL      "5.0.4"

#define NS_ALPHA_RELEASE    1
#define NS_BETA_RELEASE     2
#define NS_FINAL_RELEASE    3
#define NS_RELEASE_LEVEL    (NS_FINAL_RELEASE)
#define NS_NAVISERVER       "/usr/local/ns"

#endif /* NSVERSION_H */
