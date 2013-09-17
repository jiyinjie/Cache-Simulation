
/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

/* The code to read a memory trace and simulate it on the various
   caches. */

#include "global.h"
#include "utils.h"        // utility functions
#include "cds.h"          // Cache Description Structures
#include "caches.h"       // cache manipulation code


struct memory_reference
{
    enum memory_access_type  type;
    memory_address           address;
    unsigned int             length;
};
typedef struct memory_reference memory_reference;

static int trace_line_number;



/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

/* read on input line */

void read_reference(FILE *trace_file, memory_reference *reference)
{
    int c;
    /* we have the first character; it defined the
       memory access type.  Skip any blanks, get the
       hexadecimal address, skip the comma and get the length */

    /* skip any leading blanks */
    c = skip_blanks(trace_file);
    
    memory_address a = 0;
    while (ishex(c))
        {
            a = (a << 4) | hexvalue(c);
            c = getc(trace_file);
        }
    if (c != ',')
        {
            fprintf(stderr, "bad trace file input at line %d: %c\n", trace_line_number, c);
            exit(-1);
        }
  
    /* skip the comma */
    /* and get the length */
    int n = 0;
    c = getc(trace_file);    
    while (isdigit(c))
        {
            n = n * 10 + decvalue(c);
            c = getc(trace_file);
        }

    /* skip to end of line */
    while ((c != '\n') && (c != EOF)) c = getc(trace_file);

    /* define reference fields */
    reference->address = a;
    reference->length = n;
}



/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */


int Read_trace_file_line(FILE *trace_file, memory_reference *reference)
{
    int c;

    trace_line_number = 0;

    while ((c = getc(trace_file)) != EOF)
        {
            /* start the next line */
            trace_line_number += 1;

            /* skip any leading blanks */
            while (isspace(c) && (c != EOF)) c = getc(trace_file);

            /* what is the character ? */
            switch (c)
                {
                case 'I': /* instruction trace */
                    {
                        reference->type = MAT_FETCH;
                        read_reference(trace_file, reference);
                        return('I');
                    }

                case 'S': /* store */
                    {
                        reference->type = MAT_STORE;
                        read_reference(trace_file, reference);
                        return('S');
                    }

                case 'L': /* load */
                    {
                        reference->type = MAT_LOAD;
                        read_reference(trace_file, reference);
                        return('L');
                    }
                }

            /* apparently not a reference line.  There are a bunch
               of other lines that valgrind puts out.  They start
               with  ====, or --, or such.  Skip the entire line. */
            /* skip to end of line */
            while ((c != '\n') && (c != EOF)) c = getc(trace_file);
        }
    return(EOF);
}


/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

/* LFU counts the number of times something is used.  To prevent
   a large number from just sitting there, we cause it to decay
   over time.  Every "tick" time units, we shift left one bit,
   so that eventually a count will go to zero if it is not continuing
   to be used. */

void Check_For_Decay(CDS *cds)
{
    if (cds->replacement_policy != CRP_LFU) return;

    if ((cds->number_of_memory_reference % cds->LFU_Decay_Interval) == 0)
        {
            int i;
            if (debug) fprintf(debug_file, "%s: LFU decay for all LFU counters\n", cds->name);
            for (i = 0; i < cds->number_of_cache_entries; i++)
                {
                    cds->c[i].replacement_data = cds->c[i].replacement_data/2;
                }
        }
}

/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

int Replacement_policy(CDS *cds, int first_index, int set_size)
{
    int i;
    int victim;

    if (debug) fprintf(debug_file, "%s: look for victim in %d lines starting at %d\n", cds->name,  set_size, first_index);
    /* first look to see if any entry is empty */
    for (i = 0; i < set_size; i++)
        {
            if (!(cds->c[first_index+i].valid))
                {
                    victim = first_index+i;
                    if (debug) fprintf(debug_file, "%s: found empty cache entry at %d\n", cds->name,  victim);
                    return(victim);
                }
        }
    
    victim = first_index; /* default victim */
    switch (cds->replacement_policy)
        {
        case CRP_FIFO:  /* replacement data is the order we were brought in: 1, 2, 3, ... */
                    /* choose the smallest */
        case CRP_LRU:            
        case CRP_LFU:  /* replacement data is the number of uses, so
                      choose the smallest */
            {
                int min = cds->c[first_index].replacement_data;
                if (debug) fprintf(debug_file, "%s: replacement data: [%d]: %d", cds->name, victim, min);
                for (i = 1; i < set_size; i++)
                    {
                        if (debug) fprintf(debug_file, ", [%d]: %d", first_index+i, cds->c[first_index+i].replacement_data);
                        if (cds->c[first_index+i].replacement_data < min)
                            {
                                victim = first_index+i;
                                min = cds->c[victim].replacement_data;
                            }
                    }
                if (debug) fprintf(debug_file, "\n");
            }
            break;
                              
        case CRP_RANDOM:  
            victim = first_index + (random() % set_size);
            break;
        }

    if (debug) fprintf(debug_file, "%s: found victim in entry %d\n", cds->name,  victim);
    return(victim);
}

void Set_Replacement_Policy_Data(CDS *cds, int first_index, int i)
{
    switch (cds->replacement_policy)
        {
        case CRP_FIFO:  /* replacement data is the order we were brought in: 1, 2, 3, ... */
            cds->c[first_index+i].replacement_data = cds->number_of_memory_reference;
            break;
            
        case CRP_LFU:  /* replacement data is a count; starts at zero */
            cds->c[first_index+i].replacement_data = 0;
            Check_For_Decay(cds);
            
        case CRP_RANDOM:  
            break;
        case CRP_LRU:
            cds->c[first_index+i].replacement_data = cds->number_of_memory_reference;
        }
}

void Update_Replacement_Policy_Data(CDS *cds, int first_index, int i)
{
    switch (cds->replacement_policy)
        {
        case CRP_FIFO:  /* replacement data is the order we were brought in: 1, 2, 3, ... */
            break;
            
        case CRP_LFU:  /* replacement data is the count of the number of uses */
            cds->c[first_index+i].replacement_data += 1;
            Check_For_Decay(cds);
            
        case CRP_RANDOM:  
            break;
        case CRP_LRU:
            cds->c[first_index+i].replacement_data = cds->number_of_memory_reference;
        }
}


/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

memory_address Base_Cache_Address(CDS *cds, memory_address a)
{
    /* find number of low-order bits to mask off to find beginning cache
       line address */
    int number_of_low_order_bits = which_power(cds->cache_line_size);
    memory_address low_order_mask = mask_of(number_of_low_order_bits);
    memory_address cache_address = a & (~low_order_mask);
    return(cache_address);
}


void Simulate_Reference_to_Cache_Line(CDS *cds, memory_reference *reference)
{
    if (debug) fprintf(debug_file, "%s: Reference 0x%08X of length %d\n",
                       cds->name, reference->address, reference->length);

    /* find cache line for this reference */
    /* find number of low-order bits to mask off to find beginning cache
       line address */
    
    memory_address cache_address = Base_Cache_Address(cds, reference->address);

    /* shift off low-order offset bits and find bits for 
       indexing into cache table */
    /* the number of sets is the number of cache entries
       divided by the number of ways. */
    int number_of_low_order_bits = which_power(cds->cache_line_size);
    int number_of_sets = cds->number_of_cache_entries/cds->number_of_ways;
    int sets_bits = which_power(number_of_sets);
    memory_address sets_bits_mask = mask_of(sets_bits);
    int cache_set_index = (cache_address >> number_of_low_order_bits) & sets_bits_mask;
    int cache_entry_index = cache_set_index * cds->number_of_ways;
    
    /* index into cache table and search the number of ways to
       try to find cache line. */
    int i;
    for (i = 0; i < cds->number_of_ways; i++)
        {
            if (cds->c[cache_entry_index+i].valid && (cache_address == cds->c[cache_entry_index+i].tag))
                {
                    /* found it -- record cache hit and exit */
                    if (debug) fprintf(debug_file, "%s: Found address 0x%08X in cache line %d\n", cds->name, 
                                       reference->address, cache_entry_index+i);
                    cds->number_cache_hits += 1;

                    /* update reference specific info */
                    if (reference->type == MAT_STORE) 
                        cds->c[cache_entry_index+i].dirty = TRUE;
                    Update_Replacement_Policy_Data(cds, cache_entry_index, i);
                    return;
                }
        }

    /* Did not find it. */
    cds->number_cache_misses += 1;    

    /* Choose a victim from the set */
    int victim = Replacement_policy(cds, cache_entry_index, cds->number_of_ways);
    if (debug) fprintf(debug_file, "%s: Pick victim %d to replace\n", cds->name,  victim);
    
    /* evict victim */

    /* if victim is dirty, must write back to memory first */
    if (cds->c[victim].dirty)
        {
            if (debug) fprintf(debug_file, "%s: Write victim 0x%08X to memory\n", cds->name,  cds->c[victim].tag);
            cds->number_memory_writes += 1;
            cds->c[victim].dirty = FALSE;
        }

    /* fill in victim cache line for this new line */
    cds->c[victim].valid = TRUE;
    cds->c[victim].tag = cache_address;
    cds->c[victim].dirty = (reference->type == MAT_STORE);
    if (debug) fprintf(debug_file, "%s: Read cache line 0x%08X into entry %d\n", cds->name,  cds->c[victim].tag, victim);

    Set_Replacement_Policy_Data(cds, cache_entry_index, victim-cache_entry_index);

    /* read cache line from memory into cache table */
    cds->number_memory_reads += 1;

    if (!cds->write_back && (reference->type == MAT_STORE))
        {
            /* If it's not write-back, then it is write-thru.
               For write-thru, if it's a write, we write to memory. */
            cds->number_memory_writes += 1;
        }

}

void Simulate_Reference_to_Memory(CDS *cds, memory_reference *reference)
{
    cds->number_of_memory_reference += 1;
    cds->number_of_type[reference->type] += 1;

    /* check if the entire reference fits into just one cache line */
    if (Base_Cache_Address(cds, reference->address) == Base_Cache_Address(cds, reference->address + reference->length -1))
        {
            Simulate_Reference_to_Cache_Line(cds, reference);
        }
    else
        {
            /* reference spans two cache lines.  Convert it to two
               references: the first cache line, and the second cache line */
            memory_reference reference1;
            memory_reference reference2;
            /* easiest to compute the second part first */
            reference2.type = reference->type;
            reference2.address = Base_Cache_Address(cds, reference->address + reference->length -1);
            reference2.length = reference->address + reference->length - reference2.address;

            reference1.type = reference->type;
            reference1.address = reference->address;
            reference1.length = reference->length - reference2.length;

            /* but we do the references first, then second */
            Simulate_Reference_to_Cache_Line(cds, &reference1);
            Simulate_Reference_to_Cache_Line(cds, &reference2);
        }
}




/* ***************************************************************** */
/*                                                                   */
/*                                                                   */
/* ***************************************************************** */

/* read each input line, and then simulate that reference on each
   cache. */

void Simulate_Caches(String trace_file_name)
{
    FILE *trace_file;
    memory_reference reference;

    /* open input file */
    trace_file = fopen(trace_file_name, "r");
    if (trace_file == NULL)
        {
            fprintf (stderr,"Cannot open trace file %s\n", trace_file_name);
        }

    Init_caches_for_trace();

    while (Read_trace_file_line(trace_file, &reference) != EOF)
        {
            CDS *cds = CDS_root;
            while (cds != NULL)
                {
                    Simulate_Reference_to_Memory(cds, &reference);
                    cds = cds->next;
                }
        }
    fclose(trace_file);
}
