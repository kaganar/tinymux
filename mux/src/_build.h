// _build.h
//
// $Id: _build.h,v 1.27 2004/05/18 14:57:43 sdennis Exp $
//
// MUX 2.4
// Copyright (C) 1998 through 2004 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.  
//
#ifndef MUX_BUILD_NUM
extern char szBuildNum[];
#define MUX_BUILD_NUM szBuildNum
#endif // MUX_BUILD_NUM

#ifndef MUX_BUILD_DATE
extern char szBuildDate[];
#define MUX_BUILD_DATE szBuildDate
#endif // MUX_BUILD_DATE

#define MUX_VERSION       "2.4.0.5"      // Version number
#define MUX_RELEASE_DATE  "2004-MAY-18"  // Source release date

// Define if this release is qualified as ALPHA or BETA.
//
#define ALPHA
//#define BETA
