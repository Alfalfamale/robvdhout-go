// Copyright 2012 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "a.h"
#include "arg.h"

/*
 * Initialization for any invocation.
 */

// The usual variables.
char *goarch;
char *gobin;
char *gohostarch;
char *gohostos;
char *goos;
char *goroot;
char *workdir;
char *gochar;
char *goroot_final;
char *goversion;
char *slash;	// / for unix, \ for windows
char *default_goroot;

static bool shouldbuild(char*, char*);
static void copy(char*, char*);
static char *findgoversion(void);

// The known architecture letters.
static char *gochars = "568";

// The known architectures.
static char *okgoarch[] = {
	// same order as gochars
	"arm",
	"amd64",
	"386",
};

// The known operating systems.
static char *okgoos[] = {
	"darwin",
	"linux",
	"freebsd",
	"netbsd",
	"openbsd",
	"plan9",
	"windows",
};

static void rmworkdir(void);

// find reports the first index of p in l[0:n], or else -1.
int
find(char *p, char **l, int n)
{
	int i;
	
	for(i=0; i<n; i++)
		if(streq(p, l[i]))
			return i;
	return -1;
}

// init handles initialization of the various global state, like goroot and goarch.
void
init(void)
{
	char *p;
	int i;
	Buf b;
	
	binit(&b);

	xgetenv(&b, "GOROOT");
	if(b.len == 0) {
		if(default_goroot == nil)
			fatal("$GOROOT not set and not available");
		bwritestr(&b, default_goroot);
	}
	goroot = btake(&b);

	xgetenv(&b, "GOBIN");
	if(b.len == 0)
		bprintf(&b, "%s%sbin", goroot, slash);
	gobin = btake(&b);

	xgetenv(&b, "GOOS");
	if(b.len == 0)
		bwritestr(&b, gohostos);
	goos = btake(&b);
	if(find(goos, okgoos, nelem(okgoos)) < 0)
		fatal("unknown $GOOS %s", goos);

	p = bpathf(&b, "%s/include/u.h", goroot);
	if(!isfile(p)) {
		fatal("$GOROOT is not set correctly or not exported\n"
			"\tGOROOT=%s\n"
			"\t%s does not exist", goroot, p);
	}

	xgetenv(&b, "GOHOSTARCH");
	if(b.len > 0)
		gohostarch = btake(&b);

	if(find(gohostarch, okgoarch, nelem(okgoarch)) < 0)
		fatal("unknown $GOHOSTARCH %s", gohostarch);

	xgetenv(&b, "GOARCH");
	if(b.len == 0)
		bwritestr(&b, gohostarch);
	goarch = btake(&b);
	if((i=find(goarch, okgoarch, nelem(okgoarch))) < 0)
		fatal("unknown $GOARCH %s", goarch);
	bprintf(&b, "%c", gochars[i]);
	gochar = btake(&b);

	xgetenv(&b, "GOROOT_FINAL");
	if(b.len > 0)
		goroot_final = btake(&b);
	else
		goroot_final = goroot;
	
	xsetenv("GOROOT", goroot);
	xsetenv("GOARCH", goarch);
	xsetenv("GOOS", goos);
	
	// Make the environment more predictable.
	xsetenv("LANG", "C");
	xsetenv("LANGUAGE", "en_US.UTF8");

	goversion = findgoversion();

	workdir = xworkdir();
	xatexit(rmworkdir);

	bfree(&b);
}

// rmworkdir deletes the work directory.
static void
rmworkdir(void)
{
	if(vflag > 1)
		xprintf("rm -rf %s\n", workdir);
	xremoveall(workdir);
}

// Remove trailing spaces.
static void
chomp(Buf *b)
{
	int c;

	while(b->len > 0 && ((c=b->p[b->len-1]) == ' ' || c == '\t' || c == '\r' || c == '\n'))
		b->len--;
}


// findgoversion determines the Go version to use in the version string.
static char*
findgoversion(void)
{
	char *tag, *rev, *p;
	int i, nrev;
	Buf b, path, bmore, branch;
	Vec tags;
	
	binit(&b);
	binit(&path);
	binit(&bmore);
	binit(&branch);
	vinit(&tags);
	
	// The $GOROOT/VERSION file takes priority, for distributions
	// without the Mercurial repo.
	bpathf(&path, "%s/VERSION", goroot);
	if(isfile(bstr(&path))) {
		readfile(&b, bstr(&path));
		chomp(&b);
		goto done;
	}

	// The $GOROOT/VERSION.cache file is a cache to avoid invoking
	// hg every time we run this command.  Unlike VERSION, it gets
	// deleted by the clean command.
	bpathf(&path, "%s/VERSION.cache", goroot);
	if(isfile(bstr(&path))) {
		readfile(&b, bstr(&path));
		chomp(&b);
		goto done;
	}

	// Otherwise, use Mercurial.
	// What is the current branch?
	run(&branch, goroot, CheckExit, "hg", "identify", "-b", nil);
	chomp(&branch);

	// What are the tags along the current branch?
	tag = "";
	rev = ".";
	run(&b, goroot, CheckExit, "hg", "log", "-b", bstr(&branch), "--template", "{tags} + ", nil);
	splitfields(&tags, bstr(&b));
	nrev = 0;
	for(i=0; i<tags.len; i++) {
		p = tags.p[i];
		if(streq(p, "+"))
			nrev++;
		if(hasprefix(p, "release.") || hasprefix(p, "weekly.") || hasprefix(p, "go")) {
			tag = xstrdup(p);
			// If this tag matches the current checkout
			// exactly (no "+" yet), don't show extra
			// revision information.
			if(nrev == 0)
				rev = "";
			break;
		}
	}

	if(tag[0] == '\0') {
		// Did not find a tag; use branch name.
		bprintf(&b, "branch.%s", bstr(&branch));
		tag = btake(&b);
	}
	
	if(rev[0]) {
		// Tag is before the revision we're building.
		// Add extra information.
		run(&bmore, goroot, CheckExit, "hg", "log", "--template", " +{node|short}", "-r", rev, nil);
		chomp(&bmore);
	}
	
	bprintf(&b, "%s", tag);
	if(bmore.len > 0)
		bwriteb(&b, &bmore);

	// Cache version.
	writefile(&b, bstr(&path));

done:
	p = btake(&b);
	
	
	bfree(&b);
	bfree(&path);
	bfree(&bmore);
	bfree(&branch);
	vfree(&tags);
	
	return p;
}

/*
 * Initial tree setup.
 */

// The old tools that no longer live in $GOBIN or $GOROOT/bin.
static char *oldtool[] = {
	"5a", "5c", "5g", "5l",
	"6a", "6c", "6g", "6l",
	"8a", "8c", "8g", "8l",
	"6cov",
	"6nm",
	"cgo",
	"ebnflint",
	"goapi",
	"gofix",
	"goinstall",
	"gomake",
	"gopack",
	"gopprof",
	"gotest",
	"gotype",
	"govet",
	"goyacc",
	"quietgcc",
};

// setup sets up the tree for the initial build.
static void
setup(void)
{
	int i;
	Buf b;
	char *p;

	binit(&b);

	// Create tool directory.
	p = bpathf(&b, "%s/bin", goroot);
	if(!isdir(p))
		xmkdir(p);
	p = bpathf(&b, "%s/bin/tool", goroot);
	if(!isdir(p))
		xmkdir(p);

	// Create package directory.
	p = bpathf(&b, "%s/pkg", goroot);
	if(!isdir(p))
		xmkdir(p);
	p = bpathf(&b, "%s/pkg/%s_%s", goroot, goos, goarch);
	xremoveall(p);
	xmkdir(p);

	// Create object directory.
	// We keep it in pkg/ so that all the generated binaries
	// are in one tree.
	p = bpathf(&b, "%s/pkg/obj", goroot);
	xremoveall(p);
	xmkdir(p);

	// Remove old pre-tool binaries.
	for(i=0; i<nelem(oldtool); i++)
		xremove(bprintf(&b, "%s%s%s%s%s", goroot, slash, "bin", slash, oldtool[i]));
	
	// If $GOBIN is set and has a Go compiler, it must be cleaned.
	for(i=0; gochars[i]; i++) {
		if(isfile(bprintf(&b, "%s%s%c%s", gobin, slash, gochars[i], "g"))) {
			for(i=0; i<nelem(oldtool); i++)
				xremove(bprintf(&b, "%s%s%s", gobin, slash, oldtool[i]));
			break;
		}
	}

	bfree(&b);
}

/*
 * C library and tool building
 */

// gccargs is the gcc command line to use for compiling a single C file.
static char *gccargs[] = {
	"gcc",
	"-Wall",
	"-Wno-sign-compare",
	"-Wno-missing-braces",
	"-Wno-parentheses",
	"-Wno-unknown-pragmas",
	"-Wno-switch",
	"-Wno-comment",
	"-Werror",
	"-fno-common",
	"-ggdb",
	"-O2",
	"-c",
};

// deptab lists changes to the default dependencies for a given prefix.
// deps ending in /* read the whole directory; deps beginning with - 
// exclude files with that prefix.
static struct {
	char *prefix;  // prefix of target
	char *dep[20];  // dependency tweaks for targets with that prefix
} deptab[] = {
	{"lib9", {
		"$GOROOT/include/u.h",
		"$GOROOT/include/utf.h",
		"$GOROOT/include/fmt.h",
		"$GOROOT/include/libc.h",
		"fmt/*",
		"utf/*",
	}},
	{"libbio", {
		"$GOROOT/include/u.h",
		"$GOROOT/include/utf.h",
		"$GOROOT/include/fmt.h",
		"$GOROOT/include/libc.h",
		"$GOROOT/include/bio.h",
	}},
	{"libmach", {
		"$GOROOT/include/u.h",
		"$GOROOT/include/utf.h",
		"$GOROOT/include/fmt.h",
		"$GOROOT/include/libc.h",
		"$GOROOT/include/bio.h",
		"$GOROOT/include/ar.h",
		"$GOROOT/include/bootexec.h",
		"$GOROOT/include/mach.h",
		"$GOROOT/include/ureg_amd64.h",
		"$GOROOT/include/ureg_arm.h",
		"$GOROOT/include/ureg_x86.h",
	}},
	{"cmd/cc", {
		"-pgen.c",
		"-pswt.c",
	}},
	{"cmd/gc", {
		"-cplx.c",
		"-pgen.c",
		"-y1.tab.c",  // makefile dreg
		"opnames.h",
	}},
	{"cmd/5c", {
		"../cc/pgen.c",
		"../cc/pswt.c",
		"../5l/enam.c",
		"$GOROOT/pkg/obj/libcc.a",
	}},
	{"cmd/6c", {
		"../cc/pgen.c",
		"../cc/pswt.c",
		"../6l/enam.c",
		"$GOROOT/pkg/obj/libcc.a",
	}},
	{"cmd/8c", {
		"../cc/pgen.c",
		"../cc/pswt.c",
		"../8l/enam.c",
		"$GOROOT/pkg/obj/libcc.a",
	}},
	{"cmd/5g", {
		"../gc/cplx.c",
		"../gc/pgen.c",
		"../5l/enam.c",
		"$GOROOT/pkg/obj/libgc.a",
	}},
	{"cmd/6g", {
		"../gc/cplx.c",
		"../gc/pgen.c",
		"../6l/enam.c",
		"$GOROOT/pkg/obj/libgc.a",
	}},
	{"cmd/8g", {
		"../gc/cplx.c",
		"../gc/pgen.c",
		"../8l/enam.c",
		"$GOROOT/pkg/obj/libgc.a",
	}},
	{"cmd/5l", {
		"../ld/*",
		"enam.c",
	}},
	{"cmd/6l", {
		"../ld/*",
		"enam.c",
	}},
	{"cmd/8l", {
		"../ld/*",
		"enam.c",
	}},
	{"cmd/", {
		"$GOROOT/pkg/obj/libmach.a",
		"$GOROOT/pkg/obj/libbio.a",
		"$GOROOT/pkg/obj/lib9.a",
	}},
	{"pkg/runtime", {
		"zasm_$GOOS_$GOARCH.h",
		"zgoarch_$GOARCH.go",
		"zgoos_$GOOS.go",
		"zruntime_defs_$GOOS_$GOARCH.go",
		"zversion.go",
	}},
};

// depsuffix records the allowed suffixes for source files.
char *depsuffix[] = {
	".c",
	".h",
	".s",
	".go",
	".goc",
};

// gentab records how to generate some trivial files.
static struct {
	char *nameprefix;
	void (*gen)(char*, char*);
} gentab[] = {
	{"opnames.h", gcopnames},
	{"enam.c", mkenam},
	{"zasm_", mkzasm},
	{"zgoarch_", mkzgoarch},
	{"zgoos_", mkzgoos},
	{"zruntime_defs_", mkzruntimedefs},
	{"zversion.go", mkzversion},
};

// install installs the library, package, or binary associated with dir,
// which is relative to $GOROOT/src.
static void
install(char *dir)
{
	char *name, *p, *elem, *prefix, *exe;
	bool islib, ispkg, isgo, stale;
	Buf b, b1, path;
	Vec compile, files, link, go, missing, clean, lib, extra;
	Time ttarg, t;
	int i, j, k, n;

	binit(&b);
	binit(&b1);
	binit(&path);
	vinit(&compile);
	vinit(&files);
	vinit(&link);
	vinit(&go);
	vinit(&missing);
	vinit(&clean);
	vinit(&lib);
	vinit(&extra);
	
	// path = full path to dir.
	bpathf(&path, "%s/src/%s", goroot, dir);
	name = lastelem(dir);

	islib = hasprefix(dir, "lib") || streq(dir, "cmd/cc") || streq(dir, "cmd/gc");
	ispkg = hasprefix(dir, "pkg");
	isgo = ispkg || streq(dir, "cmd/go");

	exe = "";
	if(streq(gohostos, "windows"))
		exe = ".exe";
	
	// Start final link command line.
	// Note: code below knows that link.p[2] is the target.
	if(islib) {
		// C library.
		vadd(&link, "ar");
		vadd(&link, "rsc");
		prefix = "";
		if(!hasprefix(name, "lib"))
			prefix = "lib";
		vadd(&link, bpathf(&b, "%s/pkg/obj/%s%s.a", goroot, prefix, name));
	} else if(ispkg) {
		// Go library (package).
		vadd(&link, bpathf(&b, "%s/bin/tool/pack", goroot));
		vadd(&link, "grc");
		p = bprintf(&b, "%s/pkg/%s_%s/%s", goroot, goos, goarch, dir+4);
		*xstrrchr(p, '/') = '\0';
		xmkdirall(p);
		vadd(&link, bpathf(&b, "%s/pkg/%s_%s/%s.a", goroot, goos, goarch, dir+4));
	} else if(streq(dir, "cmd/go")) {
		// Go command.
		vadd(&link, bpathf(&b, "%s/bin/tool/%sl", goroot, gochar));
		vadd(&link, "-o");
		vadd(&link, bpathf(&b, "%s/bin/tool/go_bootstrap%s", goroot, exe));
	} else {
		// C command.
		vadd(&link, "gcc");
		vadd(&link, "-o");
		vadd(&link, bpathf(&b, "%s/bin/tool/%s%s", goroot, name, exe));
	}
	ttarg = mtime(link.p[2]);

	// Gather files that are sources for this target.
	// Everything in that directory, and any target-specific
	// additions.
	xreaddir(&files, bstr(&path));
	for(i=0; i<nelem(deptab); i++) {
		if(hasprefix(dir, deptab[i].prefix)) {
			for(j=0; (p=deptab[i].dep[j])!=nil; j++) {
				breset(&b1);
				bwritestr(&b1, p);
				bsubst(&b1, "$GOROOT", goroot);
				bsubst(&b1, "$GOOS", goos);
				bsubst(&b1, "$GOARCH", goarch);
				p = bstr(&b1);
				if(hassuffix(p, ".a")) {
					vadd(&lib, bpathf(&b, "%s", p));
					continue;
				}
				if(hassuffix(p, "/*")) {
					bpathf(&b, "%s/%s", bstr(&path), p);
					b.len -= 2;
					xreaddir(&extra, bstr(&b));
					bprintf(&b, "%s", p);
					b.len -= 2;
					for(k=0; k<extra.len; k++)
						vadd(&files, bpathf(&b1, "%s/%s", bstr(&b), extra.p[k]));
					continue;
				}
				if(hasprefix(p, "-")) {
					p++;
					n = 0;
					for(k=0; k<files.len; k++) {
						if(hasprefix(files.p[k], p))
							xfree(files.p[k]);
						else
							files.p[n++] = files.p[k];
					}
					files.len = n;
					continue;
				}				
				vadd(&files, p);
			}
		}
	}
	vuniq(&files);

	// Convert to absolute paths.
	for(i=0; i<files.len; i++) {
		if(!isabs(files.p[i])) {
			bpathf(&b, "%s/%s", bstr(&path), files.p[i]);
			xfree(files.p[i]);
			files.p[i] = btake(&b);
		}
	}

	// Is the target up-to-date?
	stale = 1;  // TODO: Decide when 0 is okay.
	n = 0;
	for(i=0; i<files.len; i++) {
		p = files.p[i];
		for(j=0; j<nelem(depsuffix); j++)
			if(hassuffix(p, depsuffix[j]))
				goto ok;
		xfree(files.p[i]);
		continue;
	ok:
		t = mtime(p);
		if(t != 0 && !hassuffix(p, ".a") && !shouldbuild(p, dir)) {
			xfree(files.p[i]);
			continue;
		}
		if(hassuffix(p, ".go"))
			vadd(&go, p);
		if(t > ttarg)
			stale = 1;
		if(t == 0) {
			vadd(&missing, p);
			files.p[n++] = files.p[i];
			continue;
		}
		files.p[n++] = files.p[i];
	}
	files.len = n;
	
	for(i=0; i<lib.len && !stale; i++)
		if(mtime(lib.p[i]) > ttarg)
			stale = 1;
		
	if(!stale)
		goto out;

	// For package runtime, copy some files into the work space.
	if(streq(dir, "pkg/runtime")) {
		copy(bpathf(&b, "%s/arch_GOARCH.h", workdir),
			bpathf(&b1, "%s/arch_%s.h", bstr(&path), goarch));
		copy(bpathf(&b, "%s/defs_GOOS_GOARCH.h", workdir),
			bpathf(&b1, "%s/defs_%s_%s.h", bstr(&path), goos, goarch));
		copy(bpathf(&b, "%s/os_GOOS.h", workdir),
			bpathf(&b1, "%s/os_%s.h", bstr(&path), goos));
		copy(bpathf(&b, "%s/signals_GOOS.h", workdir),
			bpathf(&b1, "%s/signals_%s.h", bstr(&path), goos));
	}

	// Generate any missing files; regenerate existing ones.
	for(i=0; i<files.len; i++) {
		p = files.p[i];
		elem = lastelem(p);
		for(j=0; j<nelem(gentab); j++) {
			if(hasprefix(elem, gentab[j].nameprefix)) {
				if(vflag > 1)
					xprintf("generate %s\n", p);
				gentab[j].gen(bstr(&path), p);
				// Do not add generated file to clean list.
				// In pkg/runtime, we want to be able to
				// build the package with the go tool,
				// and it assumes these generated files already
				// exist (it does not know how to build them).
				// The 'clean' command can remove
				// the generated files.
				goto built;
			}
		}
		// Did not rebuild p.
		if(find(p, missing.p, missing.len) >= 0)
			fatal("missing file %s", p);
	built:;
	}

	// One more copy for package runtime.
	// The last batch was required for the generators.
	// This one is generated.
	if(streq(dir, "pkg/runtime")) {
		copy(bpathf(&b, "%s/zasm_GOOS_GOARCH.h", workdir),
			bpathf(&b1, "%s/zasm_%s_%s.h", bstr(&path), goos, goarch));
	}
	
	// Generate .c files from .goc files.
	if(streq(dir, "pkg/runtime")) {		
		for(i=0; i<files.len; i++) {
			p = files.p[i];
			if(!hassuffix(p, ".goc"))
				continue;
			// b = path/zp but with _goarch.c instead of .goc
			bprintf(&b, "%s%sz%s", bstr(&path), slash, lastelem(p));
			b.len -= 4;
			bwritef(&b, "_%s.c", goarch);
			goc2c(p, bstr(&b));
			vadd(&files, bstr(&b));
		}
		vuniq(&files);
	}

	// Compile the files.
	for(i=0; i<files.len; i++) {
		if(!hassuffix(files.p[i], ".c") && !hassuffix(files.p[i], ".s"))
			continue;
		name = lastelem(files.p[i]);

		vreset(&compile);
		if(!isgo) {
			// C library or tool.
			vcopy(&compile, gccargs, nelem(gccargs));
			if(streq(gohostarch, "amd64"))
				vadd(&compile, "-m64");
			else if(streq(gohostarch, "386"))
				vadd(&compile, "-m32");
			if(streq(dir, "lib9"))
				vadd(&compile, "-DPLAN9PORT");
	
			vadd(&compile, "-I");
			vadd(&compile, bpathf(&b, "%s/include", goroot));
			
			vadd(&compile, "-I");
			vadd(&compile, bstr(&path));
	
			// runtime/goos.c gets the default constants hard-coded.
			if(streq(name, "goos.c")) {
				vadd(&compile, bprintf(&b, "-DGOOS=\"%s\"", goos));
				vadd(&compile, bprintf(&b, "-DGOARCH=\"%s\"", goarch));
				bprintf(&b1, "%s", goroot);
				bsubst(&b1, "\\", "\\\\");  // turn into C string
				vadd(&compile, bprintf(&b, "-DGOROOT=\"%s\"", bstr(&b1)));
				vadd(&compile, bprintf(&b, "-DGOVERSION=\"%s\"", goversion));
			}
	
			// gc/lex.c records the GOEXPERIMENT setting used during the build.
			if(streq(name, "lex.c")) {
				xgetenv(&b, "GOEXPERIMENT");
				vadd(&compile, bprintf(&b1, "-DGOEXPERIMENT=\"%s\"", bstr(&b)));
			}
		} else {
			// Supporting files for a Go package.
			if(hassuffix(files.p[i], ".s"))
				vadd(&compile, bpathf(&b, "%s/bin/tool/%sa", goroot, gochar));
			else {
				vadd(&compile, bpathf(&b, "%s/bin/tool/%sc", goroot, gochar));
				vadd(&compile, "-FVw");
			}
			vadd(&compile, "-I");
			vadd(&compile, workdir);
			vadd(&compile, bprintf(&b, "-DGOOS_%s", goos));
			vadd(&compile, bprintf(&b, "-DGOARCH_%s", goos));
		}	

		if(!isgo && streq(gohostos, "darwin")) {
			// To debug C programs on OS X, it is not enough to say -ggdb
			// on the command line.  You have to leave the object files
			// lying around too.  Leave them in pkg/obj/, which does not
			// get removed when this tool exits.
			bpathf(&b1, "%s/pkg/obj/%s", goroot, dir);
			xmkdirall(bstr(&b1));
			bpathf(&b, "%s/%s", bstr(&b1), lastelem(files.p[i]));
		} else
			bpathf(&b, "%s/%s", workdir, lastelem(files.p[i]));

		b.p[b.len-1] = 'o';  // was c or s
		vadd(&compile, "-o");
		vadd(&compile, bstr(&b));
		vadd(&compile, files.p[i]);
		bgrunv(bstr(&path), CheckExit, &compile);

		vadd(&link, bstr(&b));
		vadd(&clean, bstr(&b));
	}
	bgwait();
	
	if(isgo) {
		// The last loop was compiling individual files.
		// Hand the Go files to the compiler en masse.
		vreset(&compile);
		vadd(&compile, bpathf(&b, "%s/bin/tool/%sg", goroot, gochar));

		bpathf(&b, "%s/_go_.%s", workdir, gochar);
		vadd(&compile, "-o");
		vadd(&compile, bstr(&b));
		vadd(&clean, bstr(&b));
		vadd(&link, bstr(&b));
		
		vadd(&compile, "-p");
		if(hasprefix(dir, "pkg/"))
			vadd(&compile, dir+4);
		else
			vadd(&compile, "main");
		
		if(streq(dir, "pkg/runtime"))
			vadd(&compile, "-+");
		
		vcopy(&compile, go.p, go.len);

		runv(nil, bstr(&path), CheckExit, &compile);
	}

	if(!islib && !isgo) {
		// C binaries need the libraries explicitly, and -lm.
		vcopy(&link, lib.p, lib.len);
		vadd(&link, "-lm");
	}

	// Remove target before writing it.
	xremove(link.p[2]);

	runv(nil, nil, CheckExit, &link);

	// In package runtime, we install runtime.h and cgocall.h too,
	// for use by cgo compilation.
	if(streq(dir, "pkg/runtime")) {
		copy(bpathf(&b, "%s/pkg/%s_%s/cgocall.h", goroot, goos, goarch),
			bpathf(&b1, "%s/src/pkg/runtime/cgocall.h", goroot));
		copy(bpathf(&b, "%s/pkg/%s_%s/runtime.h", goroot, goos, goarch),
			bpathf(&b1, "%s/src/pkg/runtime/runtime.h", goroot));
	}


out:
	for(i=0; i<clean.len; i++)
		xremove(clean.p[i]);

	bfree(&b);
	bfree(&b1);
	bfree(&path);
	vfree(&compile);
	vfree(&files);
	vfree(&link);
	vfree(&go);
	vfree(&missing);
	vfree(&clean);
	vfree(&lib);
	vfree(&extra);
}

// matchfield reports whether the field matches this build.
static bool
matchfield(char *f)
{
	return streq(f, goos) || streq(f, goarch) || streq(f, "cmd_go_bootstrap");
}

// shouldbuild reports whether we should build this file.
// It applies the same rules that are used with context tags
// in package go/build, except that the GOOS and GOARCH
// can appear anywhere in the file name, not just after _.
// In particular, they can be the entire file name (like windows.c).
// We also allow the special tag cmd_go_bootstrap.
// See ../go/bootstrap.go and package go/build.
static bool
shouldbuild(char *file, char *dir)
{
	char *name, *p;
	int i, j, ret;
	Buf b;
	Vec lines, fields;
	
	// Check file name for GOOS or GOARCH.
	name = lastelem(file);
	for(i=0; i<nelem(okgoos); i++)
		if(contains(name, okgoos[i]) && !streq(okgoos[i], goos))
			return 0;
	for(i=0; i<nelem(okgoarch); i++)
		if(contains(name, okgoarch[i]) && !streq(okgoarch[i], goarch))
			return 0;
	
	// Omit test files.
	if(contains(name, "_test"))
		return 0;
	
	// cmd/go/doc.go has a giant /* */ comment before
	// it gets to the important detail that it is not part of
	// package main.  We don't parse those comments,
	// so special case that file.
	if(hassuffix(file, "cmd/go/doc.go") || hassuffix(file, "cmd\\go\\doc.go"))
		return 0;

	// Check file contents for // +build lines.
	binit(&b);
	vinit(&lines);
	vinit(&fields);

	ret = 1;
	readfile(&b, file);
	splitlines(&lines, bstr(&b));
	for(i=0; i<lines.len; i++) {
		p = lines.p[i];
		while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
			p++;
		if(*p == '\0')
			continue;
		if(contains(p, "package documentation")) {
			ret = 0;
			goto out;
		}
		if(contains(p, "package main") && !streq(dir, "cmd/go")) {
			ret = 0;
			goto out;
		}
		if(!hasprefix(p, "//"))
			break;
		if(!contains(p, "+build"))
			continue;
		splitfields(&fields, lines.p[i]);
		if(fields.len < 2 || !streq(fields.p[1], "+build"))
			continue;
		for(j=2; j<fields.len; j++) {
			p = fields.p[j];
			if((*p == '!' && !matchfield(p+1)) || matchfield(p))
				goto fieldmatch;
		}
		ret = 0;
		goto out;
	fieldmatch:;
	}

out:
	bfree(&b);
	vfree(&lines);
	vfree(&fields);
	
	return ret;
}

// copy copies the file src to dst, via memory (so only good for small files).
static void
copy(char *dst, char *src)
{
	Buf b;
	
	if(vflag > 1)
		xprintf("cp %s %s\n", src, dst);

	binit(&b);
	readfile(&b, src);
	writefile(&b, dst);
	bfree(&b);
}

// buildorder records the order of builds for the 'go bootstrap' command.
static char *buildorder[] = {
	"lib9",
	"libbio",
	"libmach",

	"cmd/cov",
	"cmd/nm",
	"cmd/pack",
	"cmd/prof",

	"cmd/cc",  // must be before c
	"cmd/gc",  // must be before g
	"cmd/%sl",  // must be before a, c, g
	"cmd/%sa",
	"cmd/%sc",
	"cmd/%sg",

	// The dependency order here was copied from a buildscript
	// back when there were build scripts.  Will have to
	// be maintained by hand, but shouldn't change very
	// often.
	"pkg/runtime",
	"pkg/errors",
	"pkg/sync/atomic",
	"pkg/sync",
	"pkg/io",
	"pkg/unicode",
	"pkg/unicode/utf8",
	"pkg/unicode/utf16",
	"pkg/bytes",
	"pkg/math",
	"pkg/strings",
	"pkg/strconv",
	"pkg/bufio",
	"pkg/sort",
	"pkg/container/heap",
	"pkg/encoding/base64",
	"pkg/syscall",
	"pkg/time",
	"pkg/os",
	"pkg/reflect",
	"pkg/fmt",
	"pkg/encoding/json",
	"pkg/encoding/gob",
	"pkg/flag",
	"pkg/path/filepath",
	"pkg/path",
	"pkg/io/ioutil",
	"pkg/log",
	"pkg/regexp/syntax",
	"pkg/regexp",
	"pkg/go/token",
	"pkg/go/scanner",
	"pkg/go/ast",
	"pkg/go/parser",
	"pkg/go/build",
	"pkg/os/exec",
	"pkg/net/url",
	"pkg/text/template/parse",
	"pkg/text/template",

	"cmd/go",
};

// cleantab records the directories to clean in 'go clean'.
// It is bigger than the buildorder because we clean all the
// compilers but build only the $GOARCH ones.
static char *cleantab[] = {
	"cmd/5a",
	"cmd/5c",
	"cmd/5g",
	"cmd/5l",
	"cmd/6a",
	"cmd/6c",
	"cmd/6g",
	"cmd/6l",
	"cmd/8a",
	"cmd/8c",
	"cmd/8g",
	"cmd/8l",
	"cmd/cc",
	"cmd/cov",
	"cmd/gc",
	"cmd/go",
	"cmd/nm",
	"cmd/pack",
	"cmd/prof",
	"lib9",
	"libbio",
	"libmach",
	"pkg/bufio",
	"pkg/bytes",
	"pkg/container/heap",
	"pkg/encoding/base64",
	"pkg/encoding/gob",
	"pkg/encoding/json",
	"pkg/errors",
	"pkg/flag",
	"pkg/fmt",
	"pkg/go/ast",
	"pkg/go/build",
	"pkg/go/parser",
	"pkg/go/scanner",
	"pkg/go/token",
	"pkg/io",
	"pkg/io/ioutil",
	"pkg/log",
	"pkg/math",
	"pkg/net/url",
	"pkg/os",
	"pkg/os/exec",
	"pkg/path",
	"pkg/path/filepath",
	"pkg/reflect",
	"pkg/regexp",
	"pkg/regexp/syntax",
	"pkg/runtime",
	"pkg/sort",
	"pkg/strconv",
	"pkg/strings",
	"pkg/sync",
	"pkg/sync/atomic",
	"pkg/syscall",
	"pkg/text/template",
	"pkg/text/template/parse",
	"pkg/time",
	"pkg/unicode",
	"pkg/unicode/utf16",
	"pkg/unicode/utf8",
};

static void
clean(void)
{
	int i, j, k;
	Buf b, path;
	Vec dir;
	
	binit(&b);
	binit(&path);
	vinit(&dir);
	
	for(i=0; i<nelem(cleantab); i++) {
		bpathf(&path, "%s/src/%s", goroot, cleantab[i]);
		xreaddir(&dir, bstr(&path));
		// Remove generated files.
		for(j=0; j<dir.len; j++) {
			for(k=0; k<nelem(gentab); k++) {
				if(hasprefix(dir.p[j], gentab[k].nameprefix))
					xremove(bpathf(&b, "%s/%s", bstr(&path), dir.p[j]));
			}
		}
		// Remove generated binary named for directory.
		if(hasprefix(cleantab[i], "cmd/"))
			xremove(bpathf(&b, "%s/%s", bstr(&path), cleantab[i]+4));
	}

	// Remove object tree.
	xremoveall(bpathf(&b, "%s/pkg/obj", goroot));
	
	// Remove installed packages and tools.
	xremoveall(bpathf(&b, "%s/pkg/%s_%s", goroot, goos, goarch));
	xremove(bpathf(&b, "%s/bin/tool", goroot));
	
	// Remove cached version info.
	xremove(bpathf(&b, "%s/VERSION.cache", goroot));

	bfree(&b);
	bfree(&path);
	vfree(&dir);
}

/*
 * command implementations
 */

void
usage(void)
{
	xprintf("usage: go tool dist [command]\n"
		"Commands are:\n"
		"\n"
		"banner         print installation banner\n"
		"bootstrap      rebuild everything\n"
		"clean          deletes all built files\n"
		"env [-p]       print environment (-p: include $PATH)\n"
		"install [dir]  install individual directory\n"
		"version        print Go version\n"
		"\n"
		"All commands take -v flags to emit extra information.\n"
	);
	xexit(2);
}

// The env command prints the default environment.
void
cmdenv(int argc, char **argv)
{
	bool pflag;
	char *sep;
	Buf b, b1;
	char *format;

	binit(&b);
	binit(&b1);

	format = "%s=\"%s\"";
	pflag = 0;
	ARGBEGIN{
	case 'p':
		pflag = 1;
		break;
	case 'v':
		vflag++;
		break;
	case 'w':
		format = "set %s=%s\n";
		break;
	default:
		usage();
	}ARGEND

	if(argc > 0)
		usage();
	
	xprintf(format, "GOROOT", goroot);
	xprintf(format, "GOARCH", goarch);
	xprintf(format, "GOOS", goos);
	if(pflag) {
		sep = ":";
		if(streq(gohostos, "windows"))
			sep = ";";
		xgetenv(&b, "PATH");
		bprintf(&b1, "%s%s%s", gobin, sep, bstr(&b));
		xprintf(format, "PATH", bstr(&b1));
	}

	bfree(&b);
	bfree(&b1);
}

// The bootstrap command runs a build from scratch,
// stopping at having installed the go_bootstrap command.
void
cmdbootstrap(int argc, char **argv)
{
	int i;
	Buf b;
	char *p;

	ARGBEGIN{
	case 'v':
		vflag++;
		break;
	default:
		usage();
	}ARGEND

	if(argc > 0)
		usage();

	clean();
	setup();
	
	binit(&b);
	for(i=0; i<nelem(buildorder); i++) {
		p = bprintf(&b, buildorder[i], gochar);
		if(vflag > 0)
			xprintf("%s\n", p);
		install(p);
	}
	bfree(&b);
}

static char*
defaulttarg(void)
{
	char *p;
	Buf pwd, src;
	
	binit(&pwd);
	binit(&src);

	xgetwd(&pwd);
	p = btake(&pwd);
	bpathf(&src, "%s/src/", goroot);
	if(!hasprefix(p, bstr(&src)))
		fatal("current directory %s is not under %s", p, bstr(&src));
	p += src.len;

	bfree(&pwd);
	bfree(&src);
	
	return p;
}

// Install installs the list of packages named on the command line.
void
cmdinstall(int argc, char **argv)
{
	int i;

	ARGBEGIN{
	case 'v':
		vflag++;
		break;
	default:
		usage();
	}ARGEND
	
	if(argc == 0)
		install(defaulttarg());

	for(i=0; i<argc; i++)
		install(argv[i]);
}

// Clean deletes temporary objects.
// Clean -i deletes the installed objects too.
void
cmdclean(int argc, char **argv)
{
	ARGBEGIN{
	case 'v':
		vflag++;
		break;
	default:
		usage();
	}ARGEND

	if(argc > 0)
		usage();

	clean();
}

// Banner prints the 'now you've installed Go' banner.
void
cmdbanner(int argc, char **argv)
{
	char *pathsep;
	Buf b, b1, search;

	ARGBEGIN{
	case 'v':
		vflag++;
		break;
	default:
		usage();
	}ARGEND

	if(argc > 0)
		usage();

	binit(&b);
	binit(&b1);
	binit(&search);
	
	xprintf("\n");
	xprintf("---\n");
	xprintf("Installed Go for %s/%s in %s\n", goos, goarch, goroot);
	xprintf("Installed commands in %s\n", gobin);

	// Check that gobin appears in $PATH.
	xgetenv(&b, "PATH");
	pathsep = ":";
	if(streq(gohostos, "windows"))
		pathsep = ";";
	bprintf(&b1, "%s%s%s", pathsep, bstr(&b), pathsep);
	bprintf(&search, "%s%s%s", pathsep, gobin, pathsep);
	if(xstrstr(bstr(&b1), bstr(&search)) == nil)
		xprintf("*** You need to add %s to your PATH.\n", gobin);

	if(streq(gohostos, "darwin")) {
		xprintf("\n"
			"On OS X the debuggers must be installed setgrp procmod.\n"
			"Read and run ./sudo.bash to install the debuggers.\n");
	}
	
	if(!streq(goroot_final, goroot)) {
		xprintf("\n"
			"The binaries expect %s to be copied or moved to %s\n",
			goroot, goroot_final);
	}

	bfree(&b);
	bfree(&b1);
	bfree(&search);
}

// Version prints the Go version.
void
cmdversion(int argc, char **argv)
{
	ARGBEGIN{
	case 'v':
		vflag++;
		break;
	default:
		usage();
	}ARGEND

	if(argc > 0)
		usage();

	xprintf("%s\n", findgoversion());
}