/*
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://mozilla.org/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is AOLserver Code and related documentation
 * distributed by AOL.
 *
 * The Initial Developer of the Original Code is America Online,
 * Inc. Portions created by AOL are Copyright (C) 1999 America Online,
 * Inc. All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License, indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under either the License or the GPL.
 */

/*
 * thread.h --
 *
 *      Private nsthread library include.
 *
 */

#ifndef THREAD_H
#define THREAD_H

#include "nsthread.h"

extern void   NsthreadsInit(void);
extern void   NsInitThreads(void);
extern void   NsInitMaster(void);
extern void   NsInitReentrant(void);
extern void   NsMutexInitNext(Ns_Mutex *mutex, const char *prefix, uintptr_t *nextPtr)
  NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2) NS_GNUC_NONNULL(3);
extern void  *NsGetLock(Ns_Mutex *mutex)   NS_GNUC_NONNULL(1);
extern void  *NsLockAlloc(void)            NS_GNUC_RETURNS_NONNULL;
extern void   NsLockFree(void *lock)       NS_GNUC_NONNULL(1);
extern void   NsLockSet(void *lock)        NS_GNUC_NONNULL(1);
extern bool   NsLockTry(void *lock)        NS_GNUC_NONNULL(1);
extern void   NsLockUnset(void *lock)      NS_GNUC_NONNULL(1);
extern void   NsCleanupTls(void **slots)   NS_GNUC_NONNULL(1);
extern void **NsGetTls(void)               NS_GNUC_RETURNS_NONNULL;
extern void   NsThreadMain(void *arg)      NS_GNUC_NORETURN;
extern void   NsCreateThread(void *arg, ssize_t stacksize, Ns_Thread *threadPtr);
extern void   NsThreadExit(void *arg)      NS_GNUC_NORETURN;
extern void  *NsThreadResult(void *arg);
extern void   NsThreadFatal(const char *func, const char *osfunc, int err)
#ifndef NS_TCL_PRE86
  NS_GNUC_NORETURN
#endif
  ;
extern void   NsThreadShutdownStarted(void);
extern const char *NsThreadLibName(void)   NS_GNUC_CONST;
extern pid_t  Ns_Fork(void);



#endif /* THREAD_H */
