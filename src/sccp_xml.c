/*!
 * \file	sccp_xml.c
 * \brief	SCCP XML Class
 * \author	Diederik de Groot <ddegroot [at] users.sourceforge.net>
 * \note	This program is free software and may be modified and distributed under the terms of the GNU Public License. 
 *		See the LICENSE file at the top of the source tree.
 * 
 * $Date$
 * $Revision$
 */
#include <config.h>
#include "common.h"
#include "sccp_xml.h"

SCCP_FILE_VERSION(__FILE__, "$Revision$")

#if defined(CS_EXPERIMENTAL_XML) && defined(HAVE_LIBXML2) && defined(HAVE_LIBXSLT) && defined(HAVE_LIBEXSLT_EXSLT_H)
#include "sccp_utils.h"

#include <asterisk/paths.h>

#if HAVE_LIBXML2
#include <libxml/tree.h>
#include <libxml/xinclude.h>
#endif

#if HAVE_LIBXSLT
#include <libxslt/xslt.h>
#include <libxslt/xsltInternals.h>
#include <libxslt/transform.h>
#include <libxslt/xsltutils.h>
#include <libxslt/extensions.h>
#if HAVE_LIBEXSLT_EXSLT_H
#include <libexslt/exslt.h>
#endif
#endif

/* forward declarations */

/* private variables */

/* external functions */
static __attribute__ ((malloc)) xmlDoc * createDoc(void)
{
	xmlDoc *doc = xmlNewDoc((const xmlChar *) "1.0");
	sccp_log(DEBUGCAT_NEWCODE) (VERBOSE_PREFIX_2 "SCCP: (createDoc) doc:%p\n", doc);
	return doc;
}

static xmlNode * createNode(const char * const name)
{
	xmlNode * node = xmlNewNode(NULL, (const xmlChar *) name);
	sccp_log(DEBUGCAT_NEWCODE) (VERBOSE_PREFIX_2 "SCCP: (addElement) name:%s -> node: %p\n", name, node);
	return node;
}

static xmlNode * addElement(xmlNode *const parentNode, const char * const name, const char *const content)
{
	xmlNode * node = xmlNewChild(parentNode, NULL, (const xmlChar *) name, (const xmlChar *) content);
	sccp_log(DEBUGCAT_NEWCODE) (VERBOSE_PREFIX_2 "SCCP: (addElement) parent:%p, name:%s, content:%s -> node: %p\n", parentNode, name, content, node);
	return node;
}

static void addProperty(xmlNode *const parentNode, const char * const key, const char *const format, ...)
{
	va_list args;
	va_start(args, format);
	char tmp[DEFAULT_PBX_STR_BUFFERSIZE];
	vsnprintf(tmp, DEFAULT_PBX_STR_BUFFERSIZE, format, args);
	va_end(args);
	
	xmlNewProp(parentNode, (const xmlChar *)key, (const xmlChar *) tmp);
}

static void setRootElement(xmlDoc * const doc, xmlNode * const node)
{
	sccp_log(DEBUGCAT_NEWCODE) (VERBOSE_PREFIX_2 "SCCP: (setRootElement) doc:%p, node:%p\n", doc, node);
	xmlDocSetRootElement(doc, node);
}

static __attribute__ ((malloc)) char * dump(xmlDoc * const doc, boolean_t indent)
{
	char *output;
	int output_len = 0;
	xmlDocDumpFormatMemoryEnc(doc, (xmlChar **)&output, &output_len, "UTF-8", indent ? 1 : 0);
	
	sccp_log(DEBUGCAT_NEWCODE) (VERBOSE_PREFIX_2 "SCCP: (dump) doc:%p -> XML:\n%s\n", doc, output);
	return output;
}

#if defined(HAVE_LIBXSLT) && defined(HAVE_LIBEXSLT_EXSLT_H)
/*
static const char * const outputfmt_ext[] = {
	[SCCP_XML_OUTPUTFMT_NULL] = "",
	[SCCP_XML_OUTPUTFMT_HTML] = "html",
	[SCCP_XML_OUTPUTFMT_XML] =  "xml",
	[SCCP_XML_OUTPUTFMT_CXML] = "cxml",
	[SCCP_XML_OUTPUTFMT_AJAX] = "ajax",
	[SCCP_XML_OUTPUTFMT_TXT] = "txt",
};

static __attribute__ ((malloc)) char * searchWebDirForFile(const char *filename, sccp_xml_outputfmt_t outputfmt, const char *extension)
{
	char filepath[PATH_MAX] = "";
	snprintf(filepath, sizeof(filepath), PBX_VARLIB "/%s_%s.%s", filename, outputfmt ? outputfmt_ext[outputfmt] : "", extension);
	if (access(filepath, F_OK ) == -1) {
		pbx_log(LOG_ERROR, "\nSCCP: (sccp_xml_searchWebDirForFile) file: '%s' could not be found\n", filepath);
		filepath[0] = '\0';
		return NULL;
	}
	return strdup(filepath);
}
*/

static const char ** convertPbxVar2XsltParams(PBX_VARIABLE_TYPE *v, const char *params[17], int *nbparams)
{
	for (; v; v = v->next) {
		params[*nbparams++] = v->name;
		params[*nbparams++] = v->value;
		
		if (*nbparams >= 16) {
			pbx_log(LOG_ERROR, "SCCP: to many xslt parameters supplied\n");
			break;
		}
	}
	return params;
}

/* rework to easy unit testing, TO MUCH INTEGRATION */
static void applyStyleSheet(xmlDoc * const doc, const char *const styleSheet, const char *const language, sccp_xml_outputfmt_t outputfmt, PBX_VARIABLE_TYPE *pbx_params)
{
	const char *params[17] = {0};
	int nbparams = 0;

	params[nbparams++] = "locales";
	params[nbparams++] = language;
	
	/* process xinclude elements. */
	if (xmlXIncludeProcess(doc) < 0) {
		xmlFreeDoc(doc);
		return;
	}

	xsltStylesheetPtr xslt = xsltLoadStylesheetPI(doc);
	if (xslt) {
		xmlSubstituteEntitiesDefault(1);
		xmlLoadExtDtdDefaultValue = 1;
		convertPbxVar2XsltParams(pbx_params, params, &nbparams);
		xmlDocPtr tmpdoc = xsltApplyStylesheet(xslt, doc, params);
		xsltFreeStylesheet(xslt);
		xmlFreeDoc(doc);
		if (!tmpdoc) {
			return;
		}
		*(xmlDoc **)&doc = tmpdoc;
	}

	/*
	char *translationFilename = searchWebDirForFile("translate", SCCP_XML_OUTPUTFMT_NULL, "xml");
	if (translationFilename) {
 		params[nbparams++] = "translationFile";
 		sccp_copy_string((char *)params[nbparams++], translationFilename, SCCP_PATH_MAX);
		sccp_free(translationFilename);
	}

	char *styleSheetFilename = searchWebDirForFile(styleSheet, outputfmt, "xsl");
	if (styleSheetFilename) {
		xsltStylesheet * const style = xsltParseStylesheetFile((const xmlChar *)styleSheetFilename);
		xmlDoc * const newdoc = xsltApplyStylesheet(style, doc, convertPbxVar2XsltParams(pbx_params, params, &nbparams));
		xsltFreeStylesheet(style);

		// switch xml doc with newdoc which got the stylesheet applied, free original xml doc
		xmlDoc *const tmpdoc = doc;
		*(xmlDoc **)&doc = newdoc;
		xmlFreeDoc(tmpdoc);

		xsltCleanupGlobals();
		sccp_free(styleSheetFilename);
	}
	*/
}
#endif

static void destroyDoc(xmlDoc *const * doc)
{
	if (doc && *doc) {
		xmlFreeDoc((xmlDoc *) * doc);
		*(xmlDoc **)doc = NULL;
	}
	xmlCleanupParser();
	xmlMemoryDump();
}

/* private functions */
static void __attribute__((constructor)) init_xml(void)
{
	xmlInitParser();
	xmlSubstituteEntitiesDefault(1);
	xmlLoadExtDtdDefaultValue = 1;
	exsltRegisterAll();
}

static void __attribute__((destructor)) destroy_xml(void)
{
	xmlCleanupParser();
	xmlMemoryDump();
	xmlCleanupGlobals();
}

/* Assign to interface */
const XMLInterface iXML = {
	.createDoc = createDoc,
	.createNode = createNode,
	.addElement = addElement,
	.addProperty = addProperty,
	.setRootElement = setRootElement,

#if defined(HAVE_LIBXSLT) && defined(HAVE_LIBEXSLT_EXSLT_H)
	//.setBaseDir = setBaseDir,
	//.getBaseDir = getBaseDir,
	.applyStyleSheet = applyStyleSheet,
#endif
	.dump = dump,
	.destroyDoc = destroyDoc,
};

#else
const XMLInterface iXML = { 0 };
#endif // defined(CS_EXPERIMENTAL_XML)


#if CS_TEST_FRAMEWORK
#include <asterisk/test.h>
AST_TEST_DEFINE(sccp_xml_test)
{
	switch(cmd) {
		case TEST_INIT:
			info->name = "xml";
			info->category = "/channels/chan_sccp/";
			info->summary = "xml tests";
			info->description = "chan-sccp-b xml tests";
			return AST_TEST_NOT_RUN;
		case TEST_EXECUTE:
			break;
	}
	
	xmlDoc * doc = iXML.createDoc();
	pbx_test_validate(test, doc != NULL);
	
	xmlNode *root= iXML.createNode("root");
	iXML.setRootElement(doc, root);
	pbx_test_validate(test, root != NULL);
	
	xmlNode *group = iXML.addElement(root, "group", NULL);
	pbx_test_validate(test, group != NULL);

	xmlNode *val1 = iXML.addElement(group, "val1", "content1");
	iXML.addProperty(val1, "type", "%s", "text_value");
	iXML.addElement(group, "val2", NULL);
	iXML.addElement(group, "val3", NULL);

	char *check = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root><group><val1 type=\"text_value\">content1</val1><val2/><val3/></group></root>\n";
	
	char *result = iXML.dump(doc, FALSE);
	pbx_test_validate(test,  0 == strcmp(check, result));
	sccp_free(result);
	
	iXML.destroyDoc(&doc);
	pbx_test_validate(test, doc == NULL);
	
	return AST_TEST_PASS;
}

static void __attribute__((constructor)) sccp_register_tests(void)
{
	AST_TEST_REGISTER(sccp_xml_test);
}

static void __attribute__((destructor)) sccp_unregister_tests(void)
{
	AST_TEST_UNREGISTER(sccp_xml_test);
}
#endif

// kate: indent-width 8; replace-tabs off; indent-mode cstyle; auto-insert-doxygen on; line-numbers on; tab-indents on; keep-extra-spaces off; auto-brackets off;
