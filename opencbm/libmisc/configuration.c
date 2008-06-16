/*
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *  Copyright 2007-2008 Spiro Trikaliotis
 *
*/

/*! ************************************************************** 
** \file libmisc/configuration.c \n
** \author Spiro Trikaliotis \n
** \version $Id: configuration.c,v 1.1 2008-06-16 19:24:28 strik Exp $ \n
** \n
** \brief Shared library / DLL for accessing the driver
**        Process configuration file
**
****************************************************************/

#include "arch.h"
#include "configuration.h"
#include "libmisc.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*! the maximum line length we will accept in a configuration file */
#define ASSUMED_MAX_LINE_LENGTH 256

/*! a convenient name for opencbm_configuration_entry_s */
typedef struct opencbm_configuration_entry_s opencbm_configuration_entry_t;

/*! this struct holds an element in the configuration file.
 * That is, it holds the equivalent of a line:
 * NAME=VALUE # Comment
 *
 * \remark
 *  If Name == NULL, then this line is NOT of the form
 *  NAME=VALUE # Comment.
 *  Instead, no equal sign is present on that line at all.
 */
struct opencbm_configuration_entry_s {
    opencbm_configuration_entry_t * Next;    /*!< pointer to the next entry; NULL if this is the last one */
    char *                          Name;    /*!< the name of this entry. This can be NULL, cf. remarks*/
    char *                          Value;   /*!< the value of this entry */
    char *                          Comment; /*!< an (optional) comment for this entry */
};


/*! a convenient name for opencbm_configuration_section_s */
typedef struct opencbm_configuration_section_s opencbm_configuration_section_t;

/*! this structs holds a complete section in a configuration file.
 * That is, it is the equivalent of a line of the form
 * [NAME] # Comment
 * and a list of the following lines (Entries) until the next section begins.
 * If the stored section name is a NULL pointer, we are in the first section,
 * before the first line of the form [...]. This is a dummy section only.
 */
struct opencbm_configuration_section_s {
    opencbm_configuration_section_t * Next;    /*!< pointer to the next section; NULL if this is the last one */
    opencbm_configuration_entry_t   * Entries; /*!< pointer to a linked list of the entries in this section */
    char *                            Name;    /*!< the name of this section */
    char *                            Comment; /*!< an (optional) comment which is on the line where the section starts, if any */
};

/*! this struct holds a complete configuration file
 * The handle to the configuration file is a pointer to this struct, actually.
 */
struct opencbm_configuration_s {
    opencbm_configuration_section_t * Sections;         /*!< pointer to a linked list of sections contained in the configuration file */
    const char *                      FileName;         /*!< the file name of the configuration file */
    const char *                      FileNameForWrite; /*!< the special file name used when the configuration file is written */

    unsigned int Changed;                               /*!< marker if this file has been changed after it has been read. 0 if no changed occurred, 1 otherwise. */
};

/*! \brief \internal allocate memory for a new configuration entry

 \param CurrentSection
   Pointer to the current section to which this entry is to be added.

 \param PreviousEntry
   Pointer to the previous entry. That is, the new entry will be added
   after this entry.
   If this is given as NULL, the new entry will be added as the first
   one.

 \param EntryName
   The name of the entry which is to be allocated. That is, this
   is the left-hand side of the equal sign.
   If this is given as NULL, there is no real entry available, but
   an (incorrect) line without an equal sign.

 \param Value
   The value of the entry. That is, this is the right-hand side of
   the equal sign (excluding the commentary).

 \param Comment
   The (optional) comment on this line. All characters (including
   whitespace and the comment delimiter '#') are included.

 \return 
   Pointer to the new configuration entry.
   NULL if we run out of memory.

 \remark
   If any of the parameters Value or Comment are given as NULL, there
   is an empty string allocated for them, anyway.
   This is not true for the parameter EntryName.
*/
static opencbm_configuration_entry_t *
entry_alloc_new(opencbm_configuration_section_t * CurrentSection,
                opencbm_configuration_entry_t * PreviousEntry,
                const char * EntryName,
                const char * Value,
                const char * Comment)
{
    opencbm_configuration_entry_t * newEntry = NULL;

    assert(CurrentSection != NULL);

    do {
        newEntry = malloc(sizeof(*newEntry));

        if (newEntry == NULL)
            break;

        memset(newEntry, 0, sizeof(*newEntry));
        newEntry->Comment = cbmlibmisc_strdup(Comment);
        newEntry->Name = cbmlibmisc_strdup(EntryName);
        newEntry->Next = NULL;

        if (Value) {
            newEntry->Value = cbmlibmisc_strdup(Value);
        }
        else {
            newEntry->Value = NULL;
        }

        if (PreviousEntry != NULL) {
            PreviousEntry->Next = newEntry;
        }
        else {
            newEntry->Next          = CurrentSection->Entries;
            CurrentSection->Entries = newEntry;
        }

    } while (0);

    return newEntry;
}

/*! \brief \internal allocate memory for a new configuration section

 \param Configuration
   Pointer to the configuration file structure to which this
   section is to be added.

 \param PreviousSection
   Pointer to the previous section. That is, the new section will be
   added after this entry.
   If this is given as NULL, the new section will be added as the first
   one.

 \param SectionName
   The name of the section which is to be allocated. That is, this
   is the name between the brackets [...] in the configuration file.
   If this is given as NULL, this is the special "first" section,
   which is an unnamed "global" section.

 \param Comment
   The (optional) comment on this line. All characters (including
   whitespace and the comment delimiter '#') are included.

 \return 
   Pointer to the new configuration section.
   NULL if we run out of memory.

 \remark
   If Comment is given as NULL, there is an empty string allocated
   for it, anyway.
   This is not true for the parameter SectionName.
*/
static opencbm_configuration_section_t *
section_alloc_new(opencbm_configuration_t * Configuration,
                  opencbm_configuration_section_t * PreviousSection,
                  const char * const SectionName,
                  const char * const Comment)
{
    opencbm_configuration_section_t * newSection = NULL;

    do {
        newSection = malloc(sizeof(*newSection));

        if (newSection == NULL) {
            break;
        }

        memset(newSection, 0, sizeof(*newSection));
        newSection->Entries = NULL;
        newSection->Comment = cbmlibmisc_strdup(Comment);
        newSection->Next = NULL;

        if (SectionName) {
            newSection->Name = cbmlibmisc_strdup(SectionName);
        }
        else {
            newSection->Name = NULL;
        }

        if (PreviousSection != NULL) {
            PreviousSection->Next = newSection;
        }
        else {
            newSection->Next        = Configuration->Sections;
            Configuration->Sections = newSection;
        }

    } while (0);

    return newSection;
}

/*! \brief \internal handle the comment when reading a line of the configuration file

 This function is an internal helper function which is called whenever a
 comment is encountered in the configuration file.

 \param Buffer
   Pointer to the configuration file structure to which this
   section is to be added.

 \return 
   1 if there was a comment on that line and it has been handled,
   0 otherwise.
*/
static unsigned int
configuration_read_line_handle_comment(char * Buffer, char ** Comment)
{
    unsigned int handledComment = 0;
    char * commentBuffer = NULL;

    do {
        if (Comment == NULL || Buffer == NULL || *Buffer == 0)
            break;

        commentBuffer = malloc(strlen(Buffer) + 1);

        if (commentBuffer == NULL) {
            break;
        }

        strcpy(commentBuffer, Buffer);

        *Comment = commentBuffer;

        handledComment = 1;

    } while (0);

    return handledComment;
}

/*! \brief \internal Read a complete line from a file
 @@@@@
*/
static char *
read_a_complete_line(FILE * File)
{
    char * buffer = NULL;
    char * addbuffer = NULL;

    unsigned int error = 1;

    do {
        unsigned int bufferLength;

        addbuffer = cbmlibmisc_stralloc(ASSUMED_MAX_LINE_LENGTH);

        if (addbuffer == NULL) {
            break;
        }

        if (fgets(addbuffer, ASSUMED_MAX_LINE_LENGTH, File) == NULL) {

            /* error or EOF, quit */

            error = ferror(File) ? 1 : 0;
            break;
        }

        /* add the addbuffer to the buffer */

        if (buffer == NULL) {
            buffer = addbuffer;
            addbuffer = NULL;
        }
        else {
            char * tmpbuffer = cbmlibmisc_strcat(buffer, addbuffer);

            cbmlibmisc_strfree(addbuffer);
            addbuffer = NULL;

            cbmlibmisc_strfree(buffer);

            buffer = tmpbuffer;

            if (tmpbuffer == NULL) {
                break;
            }

        }

        /* If there is a newline, the line is complete */

        bufferLength = strlen(buffer);

        if ( (bufferLength > 0) && buffer[bufferLength - 1] == '\n')
        {
            buffer[bufferLength - 1] = 0;

            error = 0;
            break;
        }
    } while (1);

    if (error) {
        cbmlibmisc_strfree(buffer);
        buffer = NULL;
    }
    else {

        if (buffer == NULL) {
            buffer = cbmlibmisc_strdup("");
        }
    }

    cbmlibmisc_strfree(addbuffer);

    return buffer;
}

/*! \brief \internal Read a line of the configuration file

 Get the default filename of the configuration file.

 \param Handle
   Handle to the configuration file.

 \param Comment
   Pointer to a pointer to char. In case there is a comment
   present on the line, the comment will be placed there.
   If this pointer is NULL, comments are ignored.

 \param ConfigFile
   Pointer to an opened FILE structure for the file to be read.

 \return 
   Returns a (static) buffer which holds the current line.

 \remark
   Comment lines (beginning with a '#') and comments at the end of a line are
   ignored. Additionally, SPACEs, TABs, CR and NL at the end of the line are
   ignored, too.
*/
static char *
configuration_read_line(opencbm_configuration_handle Handle, char ** Comment, FILE * ConfigFile)
{
    char * buffer = NULL;
    char * ret = NULL;

    do {
        if (Comment) {
            *Comment = NULL;
        }

        /* If we already reached the end of file, abort here */

        if (feof(ConfigFile))
            break;

        /* If we got an error, abort here */

        if (ferror(ConfigFile))
            break;

        /* Read in a line */

        buffer = read_a_complete_line(ConfigFile);

        if (buffer == NULL) {
            break;
        }

        if (buffer[0] == '#') {
            if (configuration_read_line_handle_comment(buffer, Comment)) {
                cbmlibmisc_strfree(buffer);
                break;
            }
        }
        else {
            char *p;

            ret = buffer;

            /* search for a comment and trim it if it exists */

            p = strchr(buffer, '#');

            /* If there is no comment, begin at the end of line */

            if (p == NULL)
                p = buffer + strlen(buffer);

            while (p && (p > buffer))
            {
                /* trim any spaces from the right, if available */

                switch (*--p)
                {
                case ' ':
                case '\t':
                case 13:
                case 10:
                    break;

                default:
                    configuration_read_line_handle_comment(++p, Comment);
                    *p = 0;
                    p = NULL;
                }
            }

            if (p == buffer)
                *p = 0;

            break;
        }

    } while (0);

    return ret;
}

/*! \brief \internal Parse the configuration file

 This function parses the configuration file and records its contents
 into the internal opencbm_configuration_handle structure.

 \param Handle
   Handle to the configuration file.

 \param ConfigFile
   Pointer to an opened FILE structure for the file to be read.

 \return 
   0 if the parsing succeeded,
   1 otherwise.
*/
static int
opencbm_configuration_parse_file(opencbm_configuration_handle Handle, FILE * ConfigFile)
{
    int error = 1;

    do {
        opencbm_configuration_section_t * currentSection = NULL;
        opencbm_configuration_entry_t   * previousEntry  = NULL;
        char                            * line           = NULL;

        /* First, check if we successfully opened the configuration file */

        if (Handle == NULL)
            break;

        assert(ConfigFile != NULL);

        /* Seek to the beginning of the file */

        fseek(ConfigFile, 0, SEEK_SET);


        Handle->Sections = section_alloc_new(Handle, NULL, NULL, "");
        if (Handle->Sections == NULL) {
            break;
        }

        currentSection = Handle->Sections;

        /* Now, search section after section */

        do {
            char * comment = NULL;

            if (line) {
                cbmlibmisc_strfree(line);
            }

            line = configuration_read_line(Handle, &comment, ConfigFile);

            /* assume an error, if not cleared later */

            error = 1;

            if (line == NULL && comment == NULL) {

                /* The end of the file has been reached */

                error = 0;
                break;
            }

            /* Check if we found a new section */

            if (line && (line[0] == '['))
            {
                char * sectionName = NULL;
                char * p;

                sectionName = cbmlibmisc_strdup(&line[1]);
                if (sectionName == NULL)
                    break;

                p = strrchr(sectionName, ']');

                /* This is tricky. If the current line has no closing bracket,
                 * we will ignore this. Thus, this function will "correct"
                 * this error. Note that this correction can be performed in
                 * an incorrect way. However, changes are higher the user
                 * will recognise this change and find out that he has done
                 * something wrong.
                 */
                if (p != 0) {
                    *p = 0;
                }

                /* a new section starts */

                currentSection = section_alloc_new(Handle, currentSection, sectionName, comment);
                cbmlibmisc_strfree(sectionName);

                if (currentSection == NULL) {
                    break;
                }

                /* make sure to add the new entries to this section, not after
                 * the last entry of the previous section
                 */
                previousEntry = NULL;

                error = 0;
            }
            else {
                char * entryName = NULL;
                char * value = NULL;

                /* this line is (still) part of the current section */

                if (line) {
                    char * p;

                    /* process the entry */

                    p = strchr(line, '=');

                    if (p == NULL) {

                        /* the line is not formatted correctly. It is no real entry! */

                        value = cbmlibmisc_strdup(line);
                    }
                    else {
                        /* split the line into entry name and value */

                        *p = 0;
                        entryName = cbmlibmisc_strdup(line);
                        value = cbmlibmisc_strdup(p+1);
                    }
                }

                previousEntry = entry_alloc_new(currentSection, previousEntry,
                    entryName, value, comment);

                cbmlibmisc_strfree(entryName);
                cbmlibmisc_strfree(value);
                cbmlibmisc_strfree(comment);
                comment = NULL;

                if (previousEntry == NULL) {
                    break;
                }

                error = 0;
            }

            cbmlibmisc_strfree(comment);

        } while ( ! error);

        if (line) {
            cbmlibmisc_strfree(line);
        }

    } while (0);

    return error;
}

/*! \brief \internal Write the configuration file

 This function writes back the configuration file, generating
 the data stored in the internal opencbm_configuration_handle
 structure.

 \param Handle
   Handle to the configuration file.

 \return 
   0 if the writing succeeded,
   1 otherwise.
*/
static int
opencbm_configuration_write_file(opencbm_configuration_handle Handle)
{
    FILE * configfile = NULL;

    int error = 0;

    do {
        opencbm_configuration_section_t * currentSection;

        /* First, check if we successfully opened the configuration file */

        if (Handle == NULL)
            break;

        configfile = fopen(Handle->FileNameForWrite, "wt");

        if (configfile == NULL) {
            error = 1;
            break;
        }

        /* Seek to the beginning of the file */

        fseek(configfile, 0, SEEK_SET);

        for (currentSection = Handle->Sections; 
             (currentSection != NULL) && (error == 0); 
             currentSection = currentSection->Next) {

            opencbm_configuration_entry_t * currentEntry;

            /*
             * Process all section names but the first one.
             * The first section is special as it is no real section
             */
            if (currentSection != Handle->Sections) {
                if (fprintf(configfile, "[%s]%s\n",
                    currentSection->Name, currentSection->Comment) < 0)
                {
                    error = 1;
                }
            }

            for (currentEntry = currentSection->Entries; 
                 (currentEntry != NULL) && (error == 0);
                 currentEntry = currentEntry->Next)
            {
                if (fprintf(configfile, "%s%s%s%s\n", 
                        (currentEntry->Name ? currentEntry->Name : ""),
                        (currentEntry->Name && *(currentEntry->Name)) ? "=" : "",
                        (currentEntry->Value ? currentEntry->Value : ""),
                        currentEntry->Comment) < 0)
                {
                    error = 1;
                }
            }
        }

    } while (0);

    if (configfile) {
        fclose(configfile);
    }

    do {
        if (error != 0) {
            break;
        }

        if (Handle == NULL || Handle->FileName == NULL || Handle->FileNameForWrite == NULL) {
            error = 1;
            break;
        }

        if (arch_unlink(Handle->FileName)) {
            error = 1;
            break;
        }

        if (rename(Handle->FileNameForWrite, Handle->FileName)) {
            error = 1;
            break;
        }

    } while(0);

    return error;
}

/*! \brief Open the configuration file

 Opens the configuration file so it can be used later on with
 opencbm_configuration_get_data(). If the file does not exist,
 this function fails.

 \param Filename
   The name of the configuration file to open

 \return
   Returns a handle to the configuration file which can be used
   in subsequent calls to the other configuration file functions
   for reading. Write operations are not allowed after using
   this function.

   If the configuration file does not exist, this function
   returns NULL.
*/
opencbm_configuration_handle
opencbm_configuration_open(const char * Filename)
{
    opencbm_configuration_handle handle;
    unsigned int error = 1;

    FILE * configFile = NULL;

    do {
        handle = malloc(sizeof(*handle));

        if (!handle) {
            break;
        }

        memset(handle, 0, sizeof(*handle));

        handle->Sections = NULL;

        handle->FileName = cbmlibmisc_strdup(Filename);
        handle->FileNameForWrite = cbmlibmisc_strcat(handle->FileName, ".tmp");
        handle->Changed = 0;

        if ( (handle->FileName == NULL) || (handle->FileNameForWrite == NULL)) {
            break;
        }

        configFile = fopen(handle->FileName, "rt");

        if (configFile == NULL) {
            break;
        }

        opencbm_configuration_parse_file(handle, configFile);

        fclose(configFile);

        error = 0;

    } while (0);

    if (error && handle) {
        cbmlibmisc_strfree(handle->FileName);
        cbmlibmisc_strfree(handle->FileNameForWrite);
        free(handle);
        handle = NULL;
    }

    return handle;
}


/*! \brief Creates the configuration file for reading and writing

 Opens the configuration file so it can be used later on with
 opencbm_configuration_get_data(). If the file does not exist,
 a new, empty one is created.

 \param Filename
   The name of the configuration file to open

 \return
   Returns a handle to the configuration file which can be used
   in subsequent calls to the other configuration file functions.
*/
opencbm_configuration_handle
opencbm_configuration_create(const char * Filename)
{
    opencbm_configuration_handle handle = NULL;

    do {
        handle = opencbm_configuration_open(Filename);

        if (handle == NULL) {

            FILE * filehandle;
            filehandle = fopen(Filename, "wt");

            if (filehandle == NULL)
                break;

            fclose(filehandle);

            handle = opencbm_configuration_open(Filename);
            if (handle == NULL)
                break;
        }

    } while (0);

    return handle;
}

/*! \brief Free all of the memory occupied by the configuration file

 This function frees all of the memory occupied by the configuration
 file in processor memory.
 The file is not deleted from the permanent storage (hard disk).
*/
static void
opencbm_configuration_free_all(opencbm_configuration_handle Handle)
{
    opencbm_configuration_section_t *section;

    section = Handle->Sections;

    while (section != NULL)
    {
        opencbm_configuration_section_t * lastsection = section;

        opencbm_configuration_entry_t * entry = lastsection->Entries;

        section = section->Next;

        while (entry != NULL)
        {
            opencbm_configuration_entry_t * lastentry = entry;

            entry = entry->Next;

            cbmlibmisc_strfree(lastentry->Comment);
            cbmlibmisc_strfree(lastentry->Name);
            cbmlibmisc_strfree(lastentry->Value);
            free(lastentry);
        }

        cbmlibmisc_strfree(lastsection->Comment);
        cbmlibmisc_strfree(lastsection->Name);

        free(lastsection);
    }

    cbmlibmisc_strfree(Handle->FileName);
    cbmlibmisc_strfree(Handle->FileNameForWrite);

    free(Handle);
}

/*! \brief Close the configuration file

 Closes the configuration file after it has been used.
 If it has been changed in the mean time, it is first
 stored to permanent storage.

 \param Handle
   Handle to the opened configuration file, as obtained from
   opencbm_configuration_open()

 \return
   0 if the function succeeded,
   1 otherwise.
*/
int
opencbm_configuration_close(opencbm_configuration_handle Handle)
{
    int error = 0;

    do {
        if (Handle == NULL) {
            break;
        }

        if (Handle->Changed) {
            error = opencbm_configuration_write_file(Handle);
        }

        opencbm_configuration_free_all(Handle);

    } while(0);

    return error;
}

/*! \internal \brief Find data from the configuration file

 This function searches for a specific entry in the configuration
 file and returns the value found there.

 \param Handle
   Handle to the opened configuration file, as obtained from
   opencbm_configuration_open().
 
 \param Section
   A string which holds the name of the section from where to get the data.

 \param Entry
   A string which holds the name of the entry to get.

 \param Create
   If 0, we only try to find an existing entry. If none exists, we return
   with NULL.
   If 1, we create a new entry if no entry exists.

 \return
   Returns a pointer to the data entry. If it cannot be found, this
   function returns NULL.
*/
static opencbm_configuration_entry_t *
opencbm_configuration_find_data(opencbm_configuration_handle Handle,
                                const char Section[], const char Entry[],
                                unsigned int Create)
{
    opencbm_configuration_entry_t * foundEntry = NULL;

    opencbm_configuration_section_t *currentSection = NULL;
    opencbm_configuration_section_t *lastSection = NULL;
    opencbm_configuration_entry_t   *lastEntry = NULL;

    int error = 1;

    do {
        /* Check if there is a section and an entry given */

        if (Section == NULL || Entry == NULL) {
            break;
        }

        for (currentSection = Handle->Sections;
             (currentSection != NULL) && (foundEntry == NULL);
             currentSection = currentSection->Next)
        {
            int foundSection = 0;

            if (currentSection->Name == NULL) {
                foundSection = Section == NULL;
            }
            else {
                if (Section) {
                    foundSection = (strcmp(currentSection->Name, Section) == 0);
                }
            }

            if (foundSection) {
                opencbm_configuration_entry_t * currentEntry;

                for (currentEntry = currentSection->Entries;
                     (currentEntry != NULL) && (foundEntry == NULL);
                     currentEntry = currentEntry->Next)
                {
                    if (strcmp(currentEntry->Name, Entry) == 0) {

                        /* If ReturnBufferLength is 0, we only wanted to find out if 
                         * that entry existed. Thus, report "no error" and quit.
                         */

                        foundEntry = currentEntry;
                        error = 0;
                        break;
                    }

                    /* This if() ensures that we do not add the line after
                     * some comments which most probably are meant for the next
                     * section.
                     */
                    if (currentEntry->Name != NULL) {
                        lastEntry = currentEntry;
                    }
                }

                break;
            }

            lastSection = currentSection;
        }

        if (foundEntry || Create == 0) {
            break;
        }

        if (currentSection == NULL) {

            /* there was no section with that name, generate a new one */
            
            currentSection = section_alloc_new(Handle, lastSection, Section, NULL);
        }

        foundEntry = entry_alloc_new(currentSection, lastEntry, Entry, NULL, NULL);

    } while(0);

    return foundEntry;
}

/*! \brief Read data from the configuration file

 This function searches for a specific enty in the configuration
 file and returns the value found there.

 \param Handle
   Handle to the opened configuration file, as obtained from
   opencbm_configuration_open().
 
 \param Section
   A string which holds the name of the section from where to get the data.

 \param Entry
   A string which holds the name of the entry to get.

 \param ReturnBuffer
   A buffer which holds the return value on success. If the function returns
   with something different than 0, the buffer pointer to by ReturnBuffer will
   not be changed.
   Can be NULL if ReturnBufferLength is zero, too. Cf. note below.

 \param ReturnBufferLength
   The length of the buffer pointed to by ReturnBuffer.

 \return
   Returns 0 if the data entry was found. If ReturnBufferLength != 0, the
   return value is 0 only if the buffer was large enough to hold the data.

 \note
   If ReturnBufferLength is zero, this function only tests if the Entry exists
   in the given Section. In this case, this function returns 0; otherwise, it
   returns 1.
*/
int
opencbm_configuration_get_data(opencbm_configuration_handle Handle,
                               const char Section[], const char Entry[],
                               char ** ReturnBuffer)
{
    unsigned int error = 1;

    do {
        opencbm_configuration_entry_t * entry =
            opencbm_configuration_find_data(Handle, Section, Entry, 0);

        if (entry == NULL) {
            break;
        }

        /* If ReturnBufferLength is 0, we only wanted to find out if 
         * that entry existed. Thus, report "no error" and quit.
         */

        if (ReturnBuffer != 0) {

            char * p = cbmlibmisc_strdup(entry->Value);

            if (p != NULL) {
                *ReturnBuffer = p;
                error = 0;
            }
        }

    } while (0);

    return error;
}

/*! \brief Write/Change data to/in the configuration file

 This function searches for a specific entry in the configuration
 file and changes it if it exists. If it does not exist, a new
 entry is generated.

 \param Handle
   Handle to the opened configuration file, as obtained from
   opencbm_configuration_open().
 
 \param Section
   A string which holds the name of the section where to set the data.

 \param Entry
   A string which holds the name of the entry to set.

 \param Value
   A buffer which holds the value of the entry which is to be set.

 \return
   0 if the data could be written,
   1 otherwise 
*/
int
opencbm_configuration_set_data(opencbm_configuration_handle Handle,
                               const char Section[], const char Entry[],
                               const char Value[])
{
    unsigned int error = 1;

    do {
        char * newValue = NULL;

        opencbm_configuration_entry_t * entry =
            opencbm_configuration_find_data(Handle, Section, Entry, 1);

        if (entry == NULL) {
            break;
        }

        Handle->Changed = 1;

        newValue = cbmlibmisc_strdup(Value);

        if (newValue == NULL) {
            break;
        }

        cbmlibmisc_strfree(entry->Value);
        entry->Value = newValue;

        error = 0;

    } while (0);

    return error;
}

/* @@@ #define OPENCBM_STANDALONE_TEST 1 */

#ifdef OPENCBM_STANDALONE_TEST

    #ifndef NDEBUG
        #include <crtdbg.h>
    #endif

static void
EnableCrtDebug(void)
{
#ifndef NDEBUG
    int tmpFlag;

    // Get current flag
    tmpFlag = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);

    // Turn on leak-checking bit
    tmpFlag |= _CRTDBG_LEAK_CHECK_DF;
    tmpFlag |= _CRTDBG_CHECK_ALWAYS_DF;

    tmpFlag |= _CRTDBG_ALLOC_MEM_DF;

    // Set flag to the new value
    _CrtSetDbgFlag(tmpFlag);
#endif
}

static unsigned int started_an_op = 0;

static void 
OpSuccess(void)
{
    fprintf(stderr, "success.\n");
    fflush(stderr);
    started_an_op = 0;
}

static void 
OpFail(void)
{
    fprintf(stderr, "FAILED!\n");
    fflush(stderr);
    started_an_op = 0;
}

static void
OpEnd(void)
{
    if (started_an_op)
        OpSuccess();
}

static void 
OpStart(const char * const Operation)
{
    OpEnd();

    started_an_op = 1;

    fprintf(stderr, "%s() ... ", Operation);
    fflush(stderr);
}

/*! \brief Simple test case for configuration

 This function implements a very simple test case for
 the configuration

 \return
   EXIT_SUCCESS on success,
   else EXIT_ERROR
*/
int ARCH_MAINDECL main(void)
{
    int errorcode = EXIT_FAILURE;

    EnableCrtDebug();

    do {
        char buffer[4096];

        opencbm_configuration_handle handle = NULL;

        OpStart("opencbm_configuration_create()");
        handle = opencbm_configuration_create();

        if (handle == NULL) {
            break;
        }


        OpStart("opencbm_configuration_set_data(\"SectTest\", \"EntryTest\", \"VALUE\")");
        if (opencbm_configuration_set_data(handle, "SectTest", "EntryTest", "VALUE")) {
            break;
        }

        OpStart("opencbm_configuration_set_data(\"SectTest\", \"NewTest\", \"AnotherVALUE\")");
        if (opencbm_configuration_set_data(handle, "SectTest", "NewTest", "AnotherVALUE")) {
            break;
        }


        OpStart("opencbm_configuration_get_data(handle, \"SectTest\", \"NewTest\")");
        if (opencbm_configuration_get_data(handle, "SectTest", "NewTest", buffer, sizeof(buffer)) != 0) {
            break;
        }
        OpEnd();
        fprintf(stderr, "  returned: %s\n", buffer);


        OpStart("opencbm_configuration_set_data(\"NewSect\", \"AEntryTest\", \"aVALUE\")");
        if (opencbm_configuration_set_data(handle, "NewSect", "AEntryTest", "aVALUE")) {
            break;
        }

        OpStart("opencbm_configuration_set_data(\"NewSect\", \"BNewTest\", \"bAnotherVALUE\")");
        if (opencbm_configuration_set_data(handle, "NewSect", "BNewTest", "bAnotherVALUE")) {
            break;
        }


        OpStart("opencbm_configuration_set_data(\"SectTest\", \"NewTest\", \"RewrittenVALUE\")");
        if (opencbm_configuration_set_data(handle, "SectTest", "NewTest", "RewrittenVALUE")) {
            break;
        }


        OpStart("opencbm_configuration_get_data(handle, \"SectTest\", \"NewTest\")");
        if (opencbm_configuration_get_data(handle, "SectTest", "NewTest", buffer, sizeof(buffer)) != 0) {
            break;
        }
        OpEnd();
        fprintf(stderr, "  returned: %s\n", buffer);


        OpStart("opencbm_configuration_close()");
        if (opencbm_configuration_close(handle) != 0) {
            break;
        }

        errorcode = EXIT_SUCCESS;

    } while(0);

    if (errorcode == EXIT_SUCCESS) {
        OpEnd();
    }
    else {
        OpFail();
    }

    return errorcode;
}

#endif /* #ifdef OPENCBM_STANDALONE_TEST */
