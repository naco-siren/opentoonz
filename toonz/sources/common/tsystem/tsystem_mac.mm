#include "tsystem.h"

#import <Foundation/Foundation.h>

// -[NSPersistentUIManager hasPersistentStateToRestore] answers NO when this
// default is set, and AppKit only shows its "reopen windows?" alert after a
// YES. The registration domain is enough for that lookup and, unlike a
// stored preference, is not shared with the OpenToonz application that
// lives in the same bundle.
void TSystem::disableWindowStateRestoration() {
  @autoreleasepool {
    [[NSUserDefaults standardUserDefaults] registerDefaults:@{
      @"ApplePersistenceIgnoreState" : @YES
    }];
  }
}
