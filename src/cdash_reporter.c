#include <cgreen/cdash_reporter.h>
#include <cgreen/reporter.h>
#include <cgreen/breadcrumb.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

typedef int Printer(FILE *, const char *format, ...);
typedef time_t Timer(char *strtime);
typedef double DiffTimer(time_t t1, time_t t2);

typedef struct {
	CDashInfo *cdash;
	Printer *printer;
	Timer *timer;
	DiffTimer *difftimer;
	int  pipe_fd[2];
	time_t begin;
	time_t startdatetime;
	time_t enddatetime;
	time_t teststarted;
	time_t testfinished;
	FILE *fp_reporter;
	FILE *f_reporter;
	FILE *fd_test;
} CdashMemo;

static void cdash_destroy_reporter(TestReporter *reporter);
static void cdash_reporter_suite_started(TestReporter *reporter, const char *name, const int number_of_tests);
static void cdash_reporter_testcase_started(TestReporter *reporter, const char *name);
static void assert_failed(TestReporter *reporter, const char *file, int line, const char *message, va_list arguments);
static void assert_passed(TestReporter *reporter, const char *file, int line, const char *message, va_list arguments);

static void show_failed(TestReporter *reporter, const char *file, int line, const char *message, va_list arguments);
static void show_passed(TestReporter *reporter, const char *file, int line, const char *message, va_list arguments);
static void show_incomplete(TestReporter *reporter, const char *name);

static void testcase_failed_to_complete(TestReporter *reporter, const char *name);
static void cdash_reporter_testcase_finished(TestReporter *reporter, const char *name);
static void cdash_reporter_suite_finished(TestReporter *reporter, const char *name);

static time_t cdash_build_stamp(char *sbuildstamp, const char *buildtype);
static time_t cdash_current_time(char *strtime);
static double cdash_enlapsed_time(time_t t1, time_t t2);


TestReporter *create_cdash_reporter(CDashInfo *cdash) {
	FILE *fd;
	char sbuildstamp[50];
	char strstart[30];

	if (!cdash)
		return NULL;

	TestReporter *reporter = create_reporter();
	if (!reporter)
		return NULL;

    CdashMemo *memo = (CdashMemo *) malloc(sizeof(CdashMemo));
    if (!memo)
    	return NULL;

    memo->cdash = (CDashInfo *) cdash;

    memo->printer = fprintf;
    memo->timer = cdash_current_time;
    memo->difftimer = cdash_enlapsed_time;

	if (pipe(memo->pipe_fd) < 0)
		return NULL;

    if (close(memo->pipe_fd[1]) < 0)
    	return NULL;

    fd = fdopen(memo->pipe_fd[0], "r");
    if (fd == NULL)
    	return NULL;
    memo->fp_reporter = fd;

    fd = fopen("/tmp/Test.xml", "w+");
    if (fd == NULL)
    	return NULL;
    memo->f_reporter = fd;

    memo->begin = cdash_build_stamp(sbuildstamp, memo->cdash->type);

    memo->startdatetime = cdash_current_time(strstart);

	memo->printer(memo->f_reporter,
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			" <Site BuildName=\"%s\" BuildStamp=\"%s\" Name=\"%s\" Generator=\"%s\">\n"
			"  <Testing>\n"
			"   <StartDateTime>%s</StartDateTime>\n"
			"    <TestList>\n"
			"     <Test>./Source/kwsys/kwsys.testEncode</Test>\n"
			"    </TestList>\n",
			memo->cdash->build, sbuildstamp, memo->cdash->name, "Cgreen1.0.0", strstart);

    reporter->destroy = &cdash_destroy_reporter;
	reporter->start_suite = &cdash_reporter_suite_started;
	reporter->start_test = &cdash_reporter_testcase_started;
	reporter->show_fail = &show_failed;
	reporter->show_pass = &show_passed;
	reporter->show_incomplete = &show_incomplete;
	reporter->finish_test = &cdash_reporter_testcase_finished;
	reporter->finish_suite = &cdash_reporter_suite_finished;
	reporter->memo = memo;

    return reporter;
}

static void cdash_destroy_reporter(TestReporter *reporter) {
	char endtime[30];

	CdashMemo *memo = (CdashMemo *)reporter->memo;

	memo->enddatetime = cdash_current_time(endtime);

	memo->printer(memo->f_reporter, "  <EndDateTime>%s</EndDateTime>\n"
			" <ElapsedMinutes>%.2f</ElapsedMinutes>\n"
			" </Testing>\n"
			"</Site>\n", endtime, memo->difftimer(memo->startdatetime, memo->enddatetime));

	destroy_reporter(reporter);
}


static void cdash_reporter_suite_started(TestReporter *reporter, const char *name, const int number_of_tests) {
	reporter_start(reporter, name);
}

static void cdash_reporter_testcase_started(TestReporter *reporter, const char *name) {
	CdashMemo *memo = (CdashMemo *)reporter->memo;
	fflush(memo->f_reporter);
	memo->teststarted = memo->timer(NULL);
	reporter_start(reporter, name);
}

static void show_failed(TestReporter *reporter, const char *file, int line, const char *message, va_list arguments) {
	char buffer[1000];
	float exectime;
	CdashMemo *memo = (CdashMemo *)reporter->memo;

	close(memo->pipe_fd[0]);
	memo->fd_test = fdopen(memo->pipe_fd[1], "w");

	memo->testfinished = memo->timer(NULL);

	exectime = exectime = memo->difftimer(memo->teststarted, memo->testfinished);

	const char *name = get_current_from_breadcrumb((CgreenBreadcrumb *)reporter->breadcrumb);

	memo->printer(memo->fd_test,
		   "    <Test Status=\"failed\">\n");
	memo->printer(memo->fd_test,
		   "     <Name>%s</Name>\n"
		   "      <Path>%s</Path>\n"
		   "      <FullName>%s</FullName>\n"
		   "      <FullCommandLine>at [%s] line [%d]</FullCommandLine>\n", name, file, file, file, line);
	memo->printer(memo->fd_test,
		   "      <Results>\n"
		   "       <NamedMeasurement type=\"numeric/double\" name=\"Execution Time\"><Value>%f</Value></NamedMeasurement>\n"
		   "       <NamedMeasurement type=\"text/string\" name=\"Completion Status\"><Value>Completed</Value></NamedMeasurement>\n"
		   "       <NamedMeasurement type=\"text/string\" name=\"Command Line\"><Value>%s</Value></NamedMeasurement>\n"
		   "       <Measurement>\n"
		   "        <Value>", exectime, name);
	vsprintf(buffer, (message == NULL ? "Problem" : message), arguments);
	memo->printer(memo->fd_test, "%s", buffer);
	memo->printer(memo->fd_test, "</Value>\n"
		   "       </Measurement>\n"
		   "      </Results>\n"
	       "    </Test>\n");
}

static void show_passed(TestReporter *reporter, const char *file, int line, const char *message, va_list arguments) {
	float exectime;
	CdashMemo *memo = (CdashMemo *)reporter->memo;
	const char *name = get_current_from_breadcrumb((CgreenBreadcrumb *)reporter->breadcrumb);

	close(memo->pipe_fd[0]);
	memo->fd_test = fdopen(memo->pipe_fd[1], "w");

	memo->testfinished = memo->timer(NULL);
	exectime = memo->difftimer(memo->teststarted, memo->testfinished);

	memo->printer(memo->fd_test,
		   "    <Test Status=\"passed\">\n");
	memo->printer(memo->fd_test, ""
		   "     <Name>%s</Name>\n"
		   "     <Path>%s</Path>\n"
		   "     <FullName>%s</FullName>\n"
		   "     <FullCommandLine>at [%s] line [%d]</FullCommandLine>\n", name, file, file, file, line);
	memo->printer(memo->fd_test,
		   "     <Results>\n"
		   "      <NamedMeasurement type=\"numeric/double\" name=\"Execution Time\"><Value>%f</Value></NamedMeasurement>\n"
		   "      <NamedMeasurement type=\"text/string\" name=\"Completion Status\"><Value>Completed</Value></NamedMeasurement>\n"
		   "      <NamedMeasurement type=\"text/string\" name=\"Command Line\"><Value>%s</Value></NamedMeasurement>\n"
		   "      <Measurement>\n"
		   "       <Value></Value>\n"
		   "      </Measurement>\n"
		   "     </Results>\n"
	       "    </Test>\n", exectime, name);
}

static void show_incomplete(TestReporter *reporter, const char *name) {

}


static void cdash_reporter_testcase_finished(TestReporter *reporter, const char *name) {
	CdashMemo *memo = (CdashMemo *)reporter->memo;
#define MAXLINE 80
	char line[MAXLINE];

	while(fgets(line, MAXLINE, memo->fp_reporter)) {
		fprintf(memo->f_reporter, "%s", line);
	}

	reporter_finish(reporter, name);
}


static void cdash_reporter_suite_finished(TestReporter *reporter, const char *name) {
	reporter_finish(reporter, name);
}

static time_t cdash_build_stamp(char *sbuildstamp, const char *buildtype) {
	time_t t1;
	struct tm d1;
	char s[15];

	t1 = time(0);
	localtime_r(&t1, &d1);

	strftime(s, sizeof(s), "%Y%m%d-%H%M-", &d1);
	sprintf(sbuildstamp, "%s%s", s, buildtype);

	return t1;
}

static time_t cdash_current_time(char *strtime) {
	time_t t1;
	struct tm d1;
	char s[20];
	size_t i;

	t1 = time(0);
	localtime_r(&t1, &d1);

	if(strtime == NULL)
		return t1;

	i = strftime(s, 20, "%b %d %H:%M EDT", &d1);

	strncpy(strtime, s, i+1);

	return t1;
}

static double cdash_enlapsed_time(time_t t1, time_t t2) {
	double diff;

	diff = difftime(t2, t1);
	return (diff == 0 ? 0 : (diff / 60));
}