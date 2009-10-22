/* Copyright (c) 2009 Open Information Security Foundation */

/** \file
 *  \author Breno Silva <breno.silva@gmail.com>
 */

#include "eidps-common.h"
#include "eidps.h"
#include "decode.h"
#include "detect.h"
#include "flow-var.h"
#include "decode-events.h"

#include "detect-flags.h"
#include "util-unittest.h"

/**
 *  Regex (by Brian Rectanus)
 *  flags: [!+*](SAPRFU120)[,SAPRFU120]
 */
#define PARSE_REGEX "^\\s*(?:([\\+\\*!]))?\\s*([SAPRFU120]+)(?:\\s*,\\s*([SAPRFU120]+))?\\s*$"

/**
 * Flags args[0] *(3) +(2) !(1)
 *
 */

#define MODIFIER_NOT  1
#define MODIFIER_PLUS 2
#define MODIFIER_ANY  3

static pcre *parse_regex;
static pcre_extra *parse_regex_study;

static int DetectFlagsMatch (ThreadVars *, DetectEngineThreadCtx *, Packet *, Signature *, SigMatch *);
static int DetectFlagsSetup (DetectEngineCtx *, Signature *s, SigMatch *m, char *str);
static void DetectFlagsFree(void *);

/**
 * \brief Registration function for flags: keyword
 */

void DetectFlagsRegister (void) {
    sigmatch_table[DETECT_FLAGS].name = "flags";
    sigmatch_table[DETECT_FLAGS].Match = DetectFlagsMatch;
    sigmatch_table[DETECT_FLAGS].Setup = DetectFlagsSetup;
    sigmatch_table[DETECT_FLAGS].Free  = DetectFlagsFree;
    sigmatch_table[DETECT_FLAGS].RegisterTests = FlagsRegisterTests;

    const char *eb;
    int opts = 0;
    int eo;

    parse_regex = pcre_compile(PARSE_REGEX, opts, &eb, &eo, NULL);
    if(parse_regex == NULL)
    {
        printf("pcre compile of \"%s\" failed at offset %" PRId32 ": %s\n", PARSE_REGEX, eo, eb);
        goto error;
    }

    parse_regex_study = pcre_study(parse_regex, 0, &eb);
    if(eb != NULL)
    {
        printf("pcre study failed: %s\n", eb);
        goto error;
    }

error:
    return;

}

/**
 * \internal
 * \brief This function is used to match flags on a packet with those passed via flags:
 *
 * \param t pointer to thread vars
 * \param det_ctx pointer to the pattern matcher thread
 * \param p pointer to the current packet
 * \param s pointer to the Signature
 * \param m pointer to the sigmatch
 *
 * \retval 0 no match
 * \retval 1 match
 */
static int DetectFlagsMatch (ThreadVars *t, DetectEngineThreadCtx *det_ctx, Packet *p, Signature *s, SigMatch *m)
{
    int ret = 0;
    uint8_t flags = 0;
    DetectFlagsData *de = (DetectFlagsData *)m->ctx;

    if(!de || !PKT_IS_IPV4(p) || !p || !p->tcph)
        return ret;

    flags = p->tcph->th_flags;

    flags &= (de->flags & de->ignored_flags);

    switch(de->modifier)    {
        case MODIFIER_ANY:
            if((flags & de->flags) > 0)
                return 1;
            return ret;
        case MODIFIER_PLUS:
            if(((flags & de->flags) == de->flags) && (((p->tcph->th_flags - flags) + de->ignored_flags) != 0xff))
                return 1;
            return ret;
        case MODIFIER_NOT:
            if((flags & de->flags) != de->flags)
                return 1;
            return ret;
        default:
            if((flags & de->flags) == de->flags)
                return 1;
    }

    return ret;
}

/**
 * \internal
 * \brief This function is used to parse flags options passed via flags: keyword
 *
 * \param rawstr Pointer to the user provided flags options
 *
 * \retval de pointer to DetectFlagsData on success
 * \retval NULL on failure
 */
static DetectFlagsData *DetectFlagsParse (char *rawstr)
{
    DetectFlagsData *de = NULL;
#define MAX_SUBSTRINGS 30
    int ret = 0, found = 0, ignore = 0, res = 0;
    int ov[MAX_SUBSTRINGS];
    const char *str_ptr = NULL;
    char *args[3] = { NULL, NULL, NULL };
    char *ptr;
    int i;

    ret = pcre_exec(parse_regex, parse_regex_study, rawstr, strlen(rawstr), 0, 0, ov, MAX_SUBSTRINGS);

    if (ret < 1) {
        goto error;
    }

    for (i = 0; i < (ret - 1); i++) {

        res = pcre_get_substring((char *)rawstr, ov, MAX_SUBSTRINGS,i + 1, &str_ptr);

        if (res < 0) {
            goto error;
        }

        args[i] = (char *)str_ptr;
    }

    if(args[1] == NULL)
        goto error;

    de = malloc(sizeof(DetectFlagsData));
    if (de == NULL) {
        printf("DetectFlagsSetup malloc failed\n");
        goto error;
    }

    memset(de,0,sizeof(DetectFlagsData));

    de->ignored_flags = 0xff;

    /** First parse args[0] */

    if(args[0])   {

        ptr = args[0];

        while (*ptr != '\0') {
            switch (*ptr) {
                case '!':
                    de->modifier = MODIFIER_NOT;
                    break;
                case '+':
                    de->modifier = MODIFIER_PLUS;
                    break;
                case '*':
                    de->modifier = MODIFIER_ANY;
                    break;
            }
            ptr++;
        }

    }

    /** Second parse first set of flags */

    ptr = args[1];

    while (*ptr != '\0') {
        switch (*ptr) {
            case 'S':
            case 's':
                de->flags |= TH_SYN;
                found++;
                break;
            case 'A':
            case 'a':
                de->flags |= TH_ACK;
                found++;
                break;
            case 'F':
            case 'f':
                de->flags |= TH_FIN;
                found++;
                break;
            case 'R':
            case 'r':
                de->flags |= TH_RST;
                found++;
                break;
            case 'P':
            case 'p':
                de->flags |= TH_PUSH;
                found++;
                break;
            case 'U':
            case 'u':
                de->flags |= TH_URG;
                found++;
                break;
            case '1':
                de->flags |= TH_RES1;
                found++;
                break;
            case '2':
                de->flags |= TH_RES2;
                found++;
                break;
            case '0':
                de->flags = 0;
                found++;
                return de;
            default:
                found = 0;
                break;
        }
        ptr++;
    }

    if(found == 0)
        goto error;

    /** Finally parse ignored flags */

    if(args[2])    {

        ptr = args[2];

        while (*ptr != '\0') {
            switch (*ptr) {
                case 'S':
                case 's':
                    de->ignored_flags &= ~TH_SYN;
                    ignore++;
                    break;
                case 'A':
                case 'a':
                    de->ignored_flags &= ~TH_ACK;
                    ignore++;
                    break;
                case 'F':
                case 'f':
                    de->ignored_flags &= ~TH_FIN;
                    ignore++;
                    break;
                case 'R':
                case 'r':
                    de->ignored_flags &= ~TH_RST;
                    ignore++;
                    break;
                case 'P':
                case 'p':
                    de->ignored_flags &= ~TH_PUSH;
                    ignore++;
                    break;
                case 'U':
                case 'u':
                    de->ignored_flags &= ~TH_URG;
                    ignore++;
                    break;
                case '1':
                    de->ignored_flags &= ~TH_RES1;
                    ignore++;
                    break;
                case '2':
                    de->ignored_flags &= ~TH_RES2;
                    ignore++;
                    break;
                case '0':
                    break;
                default:
                    ignore = 0;
                    break;
            }
            ptr++;
        }

        if(ignore == 0)
            goto error;
    }

    for (i = 0; i < (ret - 1); i++){
        if (args[i] != NULL) free(args[i]);
    }
    return de;

error:
    for (i = 0; i < (ret - 1); i++){
        if (args[i] != NULL) free(args[i]);
    }
    if (de) free(de);
    return NULL;
}

/**
 * \internal
 * \brief this function is used to add the parsed flags into the current signature
 *
 * \param de_ctx pointer to the Detection Engine Context
 * \param s pointer to the Current Signature
 * \param m pointer to the Current SigMatch
 * \param rawstr pointer to the user provided flags options
 *
 * \retval 0 on Success
 * \retval -1 on Failure
 */
static int DetectFlagsSetup (DetectEngineCtx *de_ctx, Signature *s, SigMatch *m, char *rawstr)
{
    DetectFlagsData *de = NULL;
    SigMatch *sm = NULL;

    de = DetectFlagsParse(rawstr);
    if (de == NULL)
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_FLAGS;
    sm->ctx = (void *)de;

    SigMatchAppend(s,m,sm);
    return 0;

error:
    if (de) free(de);
    if (sm) free(sm);
    return -1;
}

/**
 * \internal
 * \brief this function will free memory associated with DetectFlagsData
 *
 * \param de pointer to DetectFlagsData
 */
static void DetectFlagsFree(void *de_ptr) {
    DetectFlagsData *de = (DetectFlagsData *)de_ptr;
    if(de) free(de);
}

/*
 * ONLY TESTS BELOW THIS COMMENT
 */

#ifdef UNITTESTS
/**
 * \test FlagsTestParse01 is a test for a  valid flags value
 *
 *  \retval 1 on succces
 *  \retval 0 on failure
 */
static int FlagsTestParse01 (void) {
    DetectFlagsData *de = NULL;
    de = DetectFlagsParse("S");
    if (de && (de->flags == TH_SYN) ) {
        DetectFlagsFree(de);
        return 1;
    }

    return 0;
}

/**
 * \test FlagsTestParse02 is a test for an invalid flags value
 *
 *  \retval 1 on succces
 *  \retval 0 on failure
 */
static int FlagsTestParse02 (void) {
    DetectFlagsData *de = NULL;
    de = DetectFlagsParse("G");
    if (de) {
        DetectFlagsFree(de);
        return 1;
    }

    return 0;
}

/**
 * \test FlagsTestParse03 test if ACK and PUSH are set. Must return success
 *
 *  \retval 1 on success
 *  \retval 0 on failure
 */
static int FlagsTestParse03 (void) {
    Packet p;
    ThreadVars tv;
    int ret = 0;
    DetectFlagsData *de = NULL;
    SigMatch *sm = NULL;
    IPV4Hdr ipv4h;
    TCPHdr tcph;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&ipv4h, 0, sizeof(IPV4Hdr));
    memset(&tcph, 0, sizeof(TCPHdr));

    p.ip4h = &ipv4h;
    p.tcph = &tcph;
    p.tcph->th_flags = TH_ACK|TH_PUSH|TH_SYN|TH_RST;

    de = DetectFlagsParse("AP");

    if (de == NULL || (de->flags != (TH_ACK|TH_PUSH)) )
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_FLAGS;
    sm->ctx = (void *)de;

    ret = DetectFlagsMatch(&tv,NULL,&p,NULL,sm);

    if(ret) {
        if (de) free(de);
        if (sm) free(sm);
        return 1;
    }

error:
    if (de) free(de);
    if (sm) free(sm);
    return 0;
}

/**
 * \test FlagsTestParse04 check if ACK bit is set. Must fails.
 *
 *  \retval 1 on succces
 *  \retval 0 on failure
 */
static int FlagsTestParse04 (void) {
    Packet p;
    ThreadVars tv;
    int ret = 0;
    DetectFlagsData *de = NULL;
    SigMatch *sm = NULL;
    IPV4Hdr ipv4h;
    TCPHdr tcph;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&ipv4h, 0, sizeof(IPV4Hdr));
    memset(&tcph, 0, sizeof(TCPHdr));

    p.ip4h = &ipv4h;
    p.tcph = &tcph;
    p.tcph->th_flags = TH_SYN;

    de = DetectFlagsParse("A");

    if (de == NULL || de->flags != TH_ACK)
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_FLAGS;
    sm->ctx = (void *)de;

    ret = DetectFlagsMatch(&tv,NULL,&p,NULL,sm);

    if(ret) {
        if (de) free(de);
        if (sm) free(sm);
        return 1;
    }

error:
    if (de) free(de);
    if (sm) free(sm);
    return 0;
}

/**
 * \test FlagsTestParse05 test if ACK+PUSH and more flags are set. Ignore SYN and RST bits.
 *       Must fails.
 *  \retval 1 on success
 *  \retval 0 on failure
 */
static int FlagsTestParse05 (void) {
    Packet p;
    ThreadVars tv;
    int ret = 0;
    DetectFlagsData *de = NULL;
    SigMatch *sm = NULL;
    IPV4Hdr ipv4h;
    TCPHdr tcph;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&ipv4h, 0, sizeof(IPV4Hdr));
    memset(&tcph, 0, sizeof(TCPHdr));

    p.ip4h = &ipv4h;
    p.tcph = &tcph;
    p.tcph->th_flags = TH_ACK|TH_PUSH|TH_SYN|TH_RST;

    de = DetectFlagsParse("+AP,SR");

    if (de == NULL || (de->modifier != MODIFIER_PLUS) || (de->flags != (TH_ACK|TH_PUSH)) || (de->ignored_flags != (TH_SYN|TH_RST)))
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_FLAGS;
    sm->ctx = (void *)de;

    ret = DetectFlagsMatch(&tv,NULL,&p,NULL,sm);

    if(ret) {
        if (de) free(de);
        if (sm) free(sm);
        return 1;
    }

error:
    if (de) free(de);
    if (sm) free(sm);
    return 0;
}

/**
 * \test FlagsTestParse06 test if ACK+PUSH and more flags are set. Ignore URG and RST bits.
 *       Must return success.
 *  \retval 1 on success
 *  \retval 0 on failure
 */
static int FlagsTestParse06 (void) {
    Packet p;
    ThreadVars tv;
    int ret = 0;
    DetectFlagsData *de = NULL;
    SigMatch *sm = NULL;
    IPV4Hdr ipv4h;
    TCPHdr tcph;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&ipv4h, 0, sizeof(IPV4Hdr));
    memset(&tcph, 0, sizeof(TCPHdr));

    p.ip4h = &ipv4h;
    p.tcph = &tcph;
    p.tcph->th_flags = TH_ACK|TH_PUSH|TH_SYN|TH_RST;

    de = DetectFlagsParse("+AP,UR");

    if (de == NULL || (de->modifier != MODIFIER_PLUS) || (de->flags != (TH_ACK|TH_PUSH)) || ((0xff - de->ignored_flags) != (TH_URG|TH_RST)))
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_FLAGS;
    sm->ctx = (void *)de;

    ret = DetectFlagsMatch(&tv,NULL,&p,NULL,sm);

    if(ret) {
        if (de) free(de);
        if (sm) free(sm);
        return 1;
    }

error:
    if (de) free(de);
    if (sm) free(sm);
    return 0;
}

/**
 * \test FlagsTestParse07 test if SYN or RST are set. Must fails.
 *
 *  \retval 1 on success
 *  \retval 0 on failure
 */
static int FlagsTestParse07 (void) {
    Packet p;
    ThreadVars tv;
    int ret = 0;
    DetectFlagsData *de = NULL;
    SigMatch *sm = NULL;
    IPV4Hdr ipv4h;
    TCPHdr tcph;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&ipv4h, 0, sizeof(IPV4Hdr));
    memset(&tcph, 0, sizeof(TCPHdr));

    p.ip4h = &ipv4h;
    p.tcph = &tcph;
    p.tcph->th_flags = TH_SYN|TH_RST;

    de = DetectFlagsParse("*AP");

    if (de == NULL || (de->modifier != MODIFIER_ANY) || (de->flags != (TH_ACK|TH_PUSH)))
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_FLAGS;
    sm->ctx = (void *)de;

    ret = DetectFlagsMatch(&tv,NULL,&p,NULL,sm);

    if(ret) {
        if (de) free(de);
        if (sm) free(sm);
        return 1;
    }

error:
    if (de) free(de);
    if (sm) free(sm);
    return 0;
}

/**
 * \test FlagsTestParse08 test if SYN or RST are set. Must return success.
 *
 *  \retval 1 on success
 *  \retval 0 on failure
 */
static int FlagsTestParse08 (void) {
    Packet p;
    ThreadVars tv;
    int ret = 0;
    DetectFlagsData *de = NULL;
    SigMatch *sm = NULL;
    IPV4Hdr ipv4h;
    TCPHdr tcph;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&ipv4h, 0, sizeof(IPV4Hdr));
    memset(&tcph, 0, sizeof(TCPHdr));

    p.ip4h = &ipv4h;
    p.tcph = &tcph;
    p.tcph->th_flags = TH_SYN|TH_RST;

    de = DetectFlagsParse("*SA");

    if (de == NULL || (de->modifier != MODIFIER_ANY) || (de->flags != (TH_ACK|TH_SYN)))
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_FLAGS;
    sm->ctx = (void *)de;

    ret = DetectFlagsMatch(&tv,NULL,&p,NULL,sm);

    if(ret) {
        if (de) free(de);
        if (sm) free(sm);
        return 1;
    }

error:
    if (de) free(de);
    if (sm) free(sm);
    return 0;
}

/**
 * \test FlagsTestParse09 test if SYN and RST are not set. Must fails.
 *
 *  \retval 1 on success
 *  \retval 0 on failure
 */
static int FlagsTestParse09 (void) {
    Packet p;
    ThreadVars tv;
    int ret = 0;
    DetectFlagsData *de = NULL;
    SigMatch *sm = NULL;
    IPV4Hdr ipv4h;
    TCPHdr tcph;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&ipv4h, 0, sizeof(IPV4Hdr));
    memset(&tcph, 0, sizeof(TCPHdr));

    p.ip4h = &ipv4h;
    p.tcph = &tcph;
    p.tcph->th_flags = TH_SYN|TH_RST;

    de = DetectFlagsParse("!PA");

    if (de == NULL || (de->modifier != MODIFIER_NOT) || (de->flags != (TH_ACK|TH_PUSH)))
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_FLAGS;
    sm->ctx = (void *)de;

    ret = DetectFlagsMatch(&tv,NULL,&p,NULL,sm);

    if(ret) {
        if (de) free(de);
        if (sm) free(sm);
        return 1;
    }

error:
    if (de) free(de);
    if (sm) free(sm);
    return 0;
}

/**
 * \test FlagsTestParse10 test if ACK and PUSH are not set. Must return success.
 *
 *  \retval 1 on success
 *  \retval 0 on failure
 */
static int FlagsTestParse10 (void) {
    Packet p;
    ThreadVars tv;
    int ret = 0;
    DetectFlagsData *de = NULL;
    SigMatch *sm = NULL;
    IPV4Hdr ipv4h;
    TCPHdr tcph;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&ipv4h, 0, sizeof(IPV4Hdr));
    memset(&tcph, 0, sizeof(TCPHdr));

    p.ip4h = &ipv4h;
    p.tcph = &tcph;
    p.tcph->th_flags = TH_SYN|TH_RST;

    de = DetectFlagsParse("!AP");

    if (de == NULL || (de->modifier != MODIFIER_NOT) || (de->flags != (TH_ACK|TH_PUSH)))
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_FLAGS;
    sm->ctx = (void *)de;

    ret = DetectFlagsMatch(&tv,NULL,&p,NULL,sm);

    if(ret) {
        if (de) free(de);
        if (sm) free(sm);
        return 1;
    }

error:
    if (de) free(de);
    if (sm) free(sm);
    return 0;
}

/**
 * \test FlagsTestParse11 test if ACK or PUSH are set. Ignore SYN and RST. Must fails.
 *
 *  \retval 1 on success
 *  \retval 0 on failure
 */
static int FlagsTestParse11 (void) {
    Packet p;
    ThreadVars tv;
    int ret = 0;
    DetectFlagsData *de = NULL;
    SigMatch *sm = NULL;
    IPV4Hdr ipv4h;
    TCPHdr tcph;

    memset(&tv, 0, sizeof(ThreadVars));
    memset(&p, 0, sizeof(Packet));
    memset(&ipv4h, 0, sizeof(IPV4Hdr));
    memset(&tcph, 0, sizeof(TCPHdr));

    p.ip4h = &ipv4h;
    p.tcph = &tcph;
    p.tcph->th_flags = TH_SYN|TH_RST|TH_URG;

    de = DetectFlagsParse("*AP,SR");

    if (de == NULL || (de->modifier != MODIFIER_ANY) || (de->flags != (TH_ACK|TH_PUSH)) || ((0xff - de->ignored_flags) != (TH_SYN|TH_RST)))
        goto error;

    sm = SigMatchAlloc();
    if (sm == NULL)
        goto error;

    sm->type = DETECT_FLAGS;
    sm->ctx = (void *)de;

    ret = DetectFlagsMatch(&tv,NULL,&p,NULL,sm);

    if(ret) {
        if (de) free(de);
        if (sm) free(sm);
        return 1;
    }

error:
    if (de) free(de);
    if (sm) free(sm);
    return 0;
}
#endif /* UNITTESTS */

/**
 * \brief this function registers unit tests for Flags
 */
void FlagsRegisterTests(void) {
#ifdef UNITTESTS
    UtRegisterTest("FlagsTestParse01", FlagsTestParse01, 1);
    UtRegisterTest("FlagsTestParse02", FlagsTestParse02, 0);
    UtRegisterTest("FlagsTestParse03", FlagsTestParse03, 1);
    UtRegisterTest("FlagsTestParse04", FlagsTestParse04, 0);
    UtRegisterTest("FlagsTestParse05", FlagsTestParse05, 0);
    UtRegisterTest("FlagsTestParse06", FlagsTestParse06, 1);
    UtRegisterTest("FlagsTestParse07", FlagsTestParse07, 0);
    UtRegisterTest("FlagsTestParse08", FlagsTestParse08, 1);
    UtRegisterTest("FlagsTestParse09", FlagsTestParse09, 1);
    UtRegisterTest("FlagsTestParse10", FlagsTestParse10, 1);
    UtRegisterTest("FlagsTestParse11", FlagsTestParse11, 0);
#endif /* UNITTESTS */
}
