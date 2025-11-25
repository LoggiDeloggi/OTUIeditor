#include "otui_parser.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static char* str_dup(const char* s) {
#ifdef _WIN32
    return _strdup(s);
#else
    return strdup(s);
#endif
}

static OTUINode* node_new(const char* name, int indent) {
    OTUINode* n = (OTUINode*)calloc(1, sizeof(OTUINode));
    if(name) n->name = str_dup(name); else n->name = str_dup("");
    n->base_style = NULL;
    n->indent = indent;
    n->comment_before = NULL;
    n->comment_inline = NULL;
    n->states = NULL;
    n->nstates = 0;
    n->cstates = 0;
    n->events = NULL;
    n->nevents = 0;
    n->cevents = 0;
    return n;
}

static void node_add_child(OTUINode* parent, OTUINode* child) {
    if(!parent || !child) return;
    if(parent->nchildren == parent->cchildren) {
        size_t nc = parent->cchildren ? parent->cchildren * 2 : 4;
        OTUINode** nd = (OTUINode**)realloc(parent->children, nc * sizeof(OTUINode*));
        if(!nd) return;
        parent->children = nd; parent->cchildren = nc;
    }
    parent->children[parent->nchildren++] = child;
}

static void node_add_prop(OTUINode* node, const char* key, const char* value) {
    if(!node || !key || !value) return;
    if(node->nprops == node->cprops) {
        size_t nc = node->cprops ? node->cprops * 2 : 4;
        OTUIProp* nd = (OTUIProp*)realloc(node->props, nc * sizeof(OTUIProp));
        if(!nd) return;
        node->props = nd; node->cprops = nc;
    }
    node->props[node->nprops].key = str_dup(key);
    node->props[node->nprops].value = str_dup(value);
    node->props[node->nprops].comment = NULL;
    node->nprops++;
}

static void state_add_prop(OTUIState* state, const char* key, const char* value) {
    if(!state || !key || !value) return;
    if(state->nprops == state->cprops) {
        size_t nc = state->cprops ? state->cprops * 2 : 4;
        OTUIProp* nd = (OTUIProp*)realloc(state->props, nc * sizeof(OTUIProp));
        if(!nd) return;
        state->props = nd; state->cprops = nc;
    }
    state->props[state->nprops].key = str_dup(key);
    state->props[state->nprops].value = str_dup(value);
    state->props[state->nprops].comment = NULL;
    state->nprops++;
}

static OTUIState* node_add_state(OTUINode* node, const char* condition, bool negated) {
    if(!node || !condition) return NULL;
    if(node->nstates == node->cstates) {
        size_t nc = node->cstates ? node->cstates * 2 : 4;
        OTUIState* nd = (OTUIState*)realloc(node->states, nc * sizeof(OTUIState));
        if(!nd) return NULL;
        node->states = nd; node->cstates = nc;
    }
    OTUIState* state = &node->states[node->nstates];
    memset(state, 0, sizeof(OTUIState));
    state->condition = str_dup(condition);
    state->negated = negated;
    node->nstates++;
    return state;
}

static OTUIEvent* node_add_event(OTUINode* node, const char* name, const char* code, bool multiline) {
    if(!node || !name || !code) return NULL;
    if(node->nevents == node->cevents) {
        size_t nc = node->cevents ? node->cevents * 2 : 4;
        OTUIEvent* nd = (OTUIEvent*)realloc(node->events, nc * sizeof(OTUIEvent));
        if(!nd) return NULL;
        node->events = nd; node->cevents = nc;
    }
    OTUIEvent* event = &node->events[node->nevents];
    memset(event, 0, sizeof(OTUIEvent));
    event->name = str_dup(name);
    event->code = str_dup(code);
    event->multiline = multiline;
    node->nevents++;
    return event;
}

static void trim(char* s) {
    if(!s) return;
    size_t i=0; while(s[i] && isspace((unsigned char)s[i])) i++;
    if(i) memmove(s, s+i, strlen(s+i)+1);
    size_t n = strlen(s);
    while(n>0 && isspace((unsigned char)s[n-1])) s[--n]='\0';
}

static int count_indent(const char* line) {
    int c=0; for(const char* p=line; *p; ++p) { if(*p==' ') c++; else if(*p=='\t') c+=4; else break; } return c;
}

const char* otui_prop_get(const OTUINode* node, const char* key) {
    if(!node || !key) return NULL;
    for(size_t i=0;i<node->nprops;i++) if(strcmp(node->props[i].key, key)==0) return node->props[i].value;
    return NULL;
}

int otui_prop_set(OTUINode* node, const char* key, const char* value) {
    if(!node || !key || !value) return 0;
    for(size_t i=0;i<node->nprops;i++) {
        if(strcmp(node->props[i].key, key)==0) {
            char* nv = str_dup(value);
            if(!nv) return 0;
            free(node->props[i].value);
            node->props[i].value = nv;
            return 1;
        }
    }
    node_add_prop(node, key, value);
    return 1;
}

void otui_dump(const OTUINode* node, int depth, FILE* out) {
    if(!node) return;
    for(int i=0;i<depth;i++) fprintf(out, "  ");
    fprintf(out, "%s", node->name);
    if(node->nprops>0) {
        fprintf(out, " [");
        for(size_t i=0;i<node->nprops;i++) {
            fprintf(out, "%s=%s", node->props[i].key, node->props[i].value);
            if(i+1<node->nprops) fprintf(out, "; ");
        }
        fprintf(out, "]");
    }
    fprintf(out, "\n");
    // Dump states
    for(size_t i=0;i<node->nstates;i++) {
        OTUIState* state = &node->states[i];
        for(int j=0;j<depth+1;j++) fprintf(out, "  ");
        fprintf(out, "$%s%s:", state->negated ? "!" : "", state->condition);
        if(state->nprops > 0) {
            fprintf(out, " [");
            for(size_t k=0;k<state->nprops;k++) {
                fprintf(out, "%s=%s", state->props[k].key, state->props[k].value);
                if(k+1<state->nprops) fprintf(out, "; ");
            }
            fprintf(out, "]");
        }
        fprintf(out, "\n");
    }
    // Dump events
    for(size_t i=0;i<node->nevents;i++) {
        OTUIEvent* event = &node->events[i];
        for(int j=0;j<depth+1;j++) fprintf(out, "  ");
        fprintf(out, "@%s: %s%s\n", event->name, event->multiline ? "(multiline)" : "", 
                event->multiline ? "" : event->code);
    }
    for(size_t i=0;i<node->nchildren;i++) otui_dump(node->children[i], depth+1, out);
}

void otui_free(OTUINode* node) {
    if(!node) return;
    for(size_t i=0;i<node->nchildren;i++) otui_free(node->children[i]);
    free(node->children);
    for(size_t i=0;i<node->nprops;i++) {
        free(node->props[i].key);
        free(node->props[i].value);
        free(node->props[i].comment);
    }
    free(node->props);
    for(size_t i=0;i<node->nstates;i++) {
        free(node->states[i].condition);
        for(size_t j=0;j<node->states[i].nprops;j++) {
            free(node->states[i].props[j].key);
            free(node->states[i].props[j].value);
            free(node->states[i].props[j].comment);
        }
        free(node->states[i].props);
    }
    free(node->states);
    for(size_t i=0;i<node->nevents;i++) {
        free(node->events[i].name);
        free(node->events[i].code);
    }
    free(node->events);
    free(node->name);
    free(node->base_style);
    free(node->comment_before);
    free(node->comment_inline);
    free(node);
}

OTUINode* otui_parse_file(const char* filepath, char* errbuf, size_t errsz) {
    FILE* f = NULL;
#ifdef _WIN32
    fopen_s(&f, filepath, "rb");
#else
    f = fopen(filepath, "rb");
#endif
    if(!f) {
        if(errbuf && errsz) snprintf(errbuf, errsz, "cannot open file");
        return NULL;
    }

    OTUINode* root = node_new("__root__", -1);
    OTUINode* stack[256]; int stack_top = 0; stack[stack_top++] = root;
    OTUIState* current_state = NULL;
    bool line_ready = false;
    char* pending_comment = NULL;

    char line[4096]; size_t lineno=0;
    while(line_ready || fgets(line, sizeof(line), f)) {
        int indent = 0;
        char* comment_text = NULL;
        if(!line_ready) {
            lineno++;
            size_t ln = strlen(line);
            while(ln>0 && (line[ln-1]=='\n' || line[ln-1]=='\r')) line[--ln]='\0';
            
            // Calculate indent BEFORE extracting comment
            indent = count_indent(line);
            
            // Extract comment - find rightmost # preceded by whitespace
            // BUT: if line contains ':', only look for # after the value (to avoid hex colors like #FFFFFF)
            char* hash = NULL;
            size_t line_len = strlen(line);
            char* colon_in_line = strchr(line, ':');
            size_t search_start = 0;
            
            // If there's a colon, start searching after the first non-whitespace char after colon
            if (colon_in_line) {
                char* val_start = colon_in_line + 1;
                while (*val_start && (*val_start == ' ' || *val_start == '\t')) val_start++;
                
                // If value starts with #, skip past it (it's a hex color, not a comment)
                if (*val_start == '#') {
                    val_start++;
                    // Skip the hex digits
                    while (*val_start && ((*val_start >= '0' && *val_start <= '9') ||
                                          (*val_start >= 'a' && *val_start <= 'f') ||
                                          (*val_start >= 'A' && *val_start <= 'F'))) {
                        val_start++;
                    }
                }
                search_start = val_start - line;
            }
            
            for(size_t i = line_len; i > search_start; i--) {
                if(line[i-1] == '#' && (i == 1 || line[i-2] == ' ' || line[i-2] == '\t')) {
                    hash = &line[i-1];
                    break;
                }
            }
            
            if(hash) {
                *hash = '\0';
                comment_text = str_dup(hash + 1);
                trim(comment_text);
            }
            
            trim(line);
            
            // If line is empty after removing comment, save comment for next node
            if(line[0]=='\0') {
                if(comment_text) {
                    if(pending_comment) {
                        size_t len1 = strlen(pending_comment);
                        size_t len2 = strlen(comment_text);
                        char* new_comment = (char*)malloc(len1 + len2 + 2);
                        if(new_comment) {
                            sprintf(new_comment, "%s\n%s", pending_comment, comment_text);
                            free(pending_comment);
                            free(comment_text);
                            pending_comment = new_comment;
                        }
                    } else {
                        pending_comment = comment_text;
                    }
                }
                continue;
            }
        } else {
            // For line_ready, calculate indent from the line we saved
            indent = count_indent(line);
        }
        line_ready = false;
        
        char* content = line;
        while(*content==' ' || *content=='\t') content++;
        
        // Check for event definition: @onClick: or @onClick: |
        if(content[0] == '@') {
            char* colon_pos = strchr(content, ':');
            if(colon_pos) {
                *colon_pos = '\0';
                char event_name_buf[256];
                strncpy(event_name_buf, content + 1, sizeof(event_name_buf) - 1);
                event_name_buf[sizeof(event_name_buf) - 1] = '\0';
                trim(event_name_buf);
                
                char* event_code = colon_pos + 1;
                trim(event_code);
                
                bool multiline = false;
                
                // Check for multiline indicator '|'
                if(event_code[0] == '|') {
                    multiline = true;
                    size_t code_len = 0;
                    size_t code_cap = 1024;
                    char* full_code = (char*)malloc(code_cap);
                    if(!full_code) {
                        if(errbuf && errsz) snprintf(errbuf, errsz, "memory error at line %zu", lineno);
                        otui_free(root); fclose(f); return NULL;
                    }
                    full_code[0] = '\0';
                    
                    // Get current widget from stack
                    OTUINode* widget_node = stack_top > 0 ? stack[stack_top-1] : NULL;
                    if(!widget_node || widget_node == root) {
                        free(full_code);
                        if(errbuf && errsz) snprintf(errbuf, errsz, "event outside node at line %zu", lineno);
                        otui_free(root); fclose(f); return NULL;
                    }
                    
                    char next_line[4096];
                    int event_base_indent = widget_node->indent;  // Event code must be indented more than the widget
                    // fprintf(stderr, "DEBUG: Starting multiline event, widget=%s, widget_indent=%d\n", widget_node->name, event_base_indent);
                    // Read multiline event code
                    while(fgets(next_line, sizeof(next_line), f)) {
                        lineno++;
                        size_t ln = strlen(next_line);
                        while(ln>0 && (next_line[ln-1]=='\n' || next_line[ln-1]=='\r')) next_line[--ln]='\0';
                        
                        int line_indent = count_indent(next_line);
                        char* next_content = next_line + line_indent;
                        
                        // fprintf(stderr, "  Line %zu: indent=%d, content='%s'\n", lineno, line_indent, next_content);
                        
                        // Empty line - skip
                        if(*next_content == '\0') {
                            // fprintf(stderr, "    -> Empty line, skipping\n");
                            continue;
                        }
                        
                        // If indentation is not deeper than event level, we're done
                        if(line_indent <= event_base_indent) {
                            // fprintf(stderr, "    -> End of multiline (indent %d <= %d), setting line_ready\n", line_indent, event_base_indent);
                            // fprintf(stderr, "    -> Copying next_line='%s' to line buffer\n", next_line);
                            strcpy(line, next_line);
                            // fprintf(stderr, "    -> line buffer now='%s', indent should be %d\n", line, line_indent);
                            line_ready = true;
                            break;
                        }
                        
                        // fprintf(stderr, "    -> Adding to event code\n");
                        
                        // Append line to event code
                        size_t needed = code_len + strlen(next_line) + 2;
                        if(needed > code_cap) {
                            code_cap *= 2;
                            char* new_code = (char*)realloc(full_code, code_cap);
                            if(!new_code) {
                                free(full_code);
                                if(errbuf && errsz) snprintf(errbuf, errsz, "memory error at line %zu", lineno);
                                otui_free(root); fclose(f); return NULL;
                            }
                            full_code = new_code;
                        }
                        
                        if(code_len > 0) {
                            full_code[code_len++] = '\n';
                        }
                        strcpy(full_code + code_len, next_line);
                        code_len += strlen(next_line);
                    }
                    
                    // Add event to current node
                    OTUINode* cur = stack_top > 0 ? stack[stack_top-1] : NULL;
                    if(!cur || cur == root) {
                        free(full_code);
                        if(errbuf && errsz) snprintf(errbuf, errsz, "event outside node at line %zu", lineno);
                        otui_free(root); fclose(f); return NULL;
                    }
                    
                    node_add_event(cur, event_name_buf, full_code, multiline);
                    free(full_code);
                    
                    // If we have a line ready, it will be processed in next iteration
                    // We must continue here to avoid processing with stale content pointer
                    if(line_ready) {
                        continue;
                    }
                } else {
                    // Single line event
                    OTUINode* cur = stack_top > 0 ? stack[stack_top-1] : NULL;
                    if(!cur || cur == root) {
                        if(errbuf && errsz) snprintf(errbuf, errsz, "event outside node at line %zu", lineno);
                        otui_free(root); fclose(f); return NULL;
                    }
                    
                    node_add_event(cur, event_name_buf, event_code, multiline);
                }
                
                continue;
            }
        }
        
        // Check for state definition: $hover: or $!on:
        if(content[0] == '$') {
            char* colon_pos = strchr(content, ':');
            if(colon_pos) {
                *colon_pos = '\0';
                char* cond = content + 1;
                bool negated = false;
                
                // Check for negation
                if(cond[0] == '!') {
                    negated = true;
                    cond++;
                }
                
                // Parse combined conditions like "pressed !disabled"
                // For now, just take the first word
                char* space = strchr(cond, ' ');
                if(space) *space = '\0';
                
                trim(cond);
                
                // Get current node
                OTUINode* cur = stack_top > 0 ? stack[stack_top-1] : NULL;
                if(!cur || cur == root) {
                    if(errbuf && errsz) snprintf(errbuf, errsz, "state outside node at line %zu", lineno);
                    otui_free(root); fclose(f); return NULL;
                }
                
                // Create state and keep it as current
                current_state = node_add_state(cur, cond, negated);
                continue;
            }
        }
        
        // Check if we're at a new node (no colon, indent at node level)
        char* colon = strchr(content, ':');
        if(!colon) {
            // New node - close any current state
            current_state = NULL;
            
            // Check for inheritance: "WidgetName < BaseStyle"
            char* less_than = strchr(content, '<');
            char* name = content;
            char* base_style = NULL;
            
            if(less_than) {
                *less_than = '\0';
                base_style = less_than + 1;
                trim(base_style);
            }
            
            trim(name);
            // pop stack if indent <= previous indent
            // DEBUG: Print stack state
            // fprintf(stderr, "Line %zu: Processing node '%s' (indent=%d)\n", lineno, name, indent);
            // fprintf(stderr, "  Stack before pop: ");
            // for(int i=0; i<stack_top; i++) fprintf(stderr, "%s(%d) ", stack[i]->name, stack[i]->indent);
            // fprintf(stderr, "\n");
            
            while(stack_top>0) {
                OTUINode* prev = stack[stack_top-1];
                if(prev->indent < indent) break;
                stack_top--;
            }
            
            // fprintf(stderr, "  Stack after pop: ");
            // for(int i=0; i<stack_top; i++) fprintf(stderr, "%s(%d) ", stack[i]->name, stack[i]->indent);
            // fprintf(stderr, "\n");
            
            if(stack_top==0) { // malformed
                if(errbuf && errsz) snprintf(errbuf, errsz, "indent error at line %zu", lineno);
                otui_free(root); fclose(f); return NULL;
            }
            OTUINode* parent = stack[stack_top-1];
            OTUINode* node = node_new(name, indent);
            if(base_style) {
                node->base_style = str_dup(base_style);
            }
            if(pending_comment) {
                node->comment_before = pending_comment;
                pending_comment = NULL;
            }
            if(comment_text) {
                node->comment_inline = comment_text;
                comment_text = NULL;
            }
            node_add_child(parent, node);
            stack[stack_top++] = node;
        } else {
            *colon='\0';
            char* key = content; trim(key);
            char* val = colon+1; trim(val);
            // property attaches to current state or node
            OTUINode* cur = stack_top>0 ? stack[stack_top-1] : NULL;
            if(!cur || cur==root) {
                if(errbuf && errsz) snprintf(errbuf, errsz, "property outside node at line %zu", lineno);
                otui_free(root); fclose(f); return NULL;
            }
            
            // Add to state if we're in one, otherwise to node
            if(current_state) {
                state_add_prop(current_state, key, val);
                if(comment_text && current_state->nprops > 0) {
                    current_state->props[current_state->nprops-1].comment = comment_text;
                    comment_text = NULL;
                }
            } else {
                node_add_prop(cur, key, val);
                if(comment_text && cur->nprops > 0) {
                    cur->props[cur->nprops-1].comment = comment_text;
                    comment_text = NULL;
                }
            }
        }
    }

    fclose(f);
    return root;
}

static void save_node(const OTUINode* node, FILE* f) {
    if(!node) return;
    if(strcmp(node->name, "__root__")==0) {
        for(size_t i=0;i<node->nchildren;i++) save_node(node->children[i], f);
        return;
    }
    // Write comment before node
    if(node->comment_before) {
        for(int i=0;i<node->indent;i++) fputc(' ', f);
        fprintf(f, "# %s\n", node->comment_before);
    }
    // Write node name
    for(int i=0;i<node->indent;i++) fputc(' ', f);
    fprintf(f, "%s", node->name);
    if(node->base_style) {
        fprintf(f, " < %s", node->base_style);
    }
    if(node->comment_inline) {
        fprintf(f, "  # %s", node->comment_inline);
    }
    fprintf(f, "\n");
    // Write properties with comments
    for(size_t i=0;i<node->nprops;i++) {
        for(int j=0;j<node->indent+2;j++) fputc(' ', f);
        fprintf(f, "%s: %s", node->props[i].key, node->props[i].value);
        if(node->props[i].comment) {
            fprintf(f, "  # %s", node->props[i].comment);
        }
        fprintf(f, "\n");
    }
    // Save states
    for(size_t i=0;i<node->nstates;i++) {
        OTUIState* state = &node->states[i];
        for(int j=0;j<node->indent+2;j++) fputc(' ', f);
        fprintf(f, "$%s%s:\n", state->negated ? "!" : "", state->condition);
        for(size_t k=0;k<state->nprops;k++) {
            for(int j=0;j<node->indent+4;j++) fputc(' ', f);
            fprintf(f, "%s: %s", state->props[k].key, state->props[k].value);
            if(state->props[k].comment) {
                fprintf(f, "  # %s", state->props[k].comment);
            }
            fprintf(f, "\n");
        }
    }
    // Save events
    for(size_t i=0;i<node->nevents;i++) {
        OTUIEvent* event = &node->events[i];
        for(int j=0;j<node->indent+2;j++) fputc(' ', f);
        fprintf(f, "@%s:", event->name);
        if(event->multiline) {
            fprintf(f, " |\n");
            // Write each line of code with proper indentation
            char* code_copy = str_dup(event->code);
            char* line_start = code_copy;
            char* line_end;
            while((line_end = strchr(line_start, '\n')) != NULL) {
                *line_end = '\0';
                fprintf(f, "%s\n", line_start);
                line_start = line_end + 1;
            }
            // Last line (if no newline at end)
            if(*line_start) {
                fprintf(f, "%s\n", line_start);
            }
            free(code_copy);
        } else {
            fprintf(f, " %s\n", event->code);
        }
    }
    for(size_t i=0;i<node->nchildren;i++) save_node(node->children[i], f);
}

int otui_save(const OTUINode* root, const char* filepath, char* errbuf, size_t errsz) {
    if(!root || !filepath) return 0;
    // criar backup .bak se existir
    FILE* existing = NULL;
#ifdef _WIN32
    fopen_s(&existing, filepath, "rb");
#else
    existing = fopen(filepath, "rb");
#endif
    if(existing) {
        fclose(existing);
        char bak[1024];
        snprintf(bak, sizeof(bak), "%s.bak", filepath);
        FILE* src = NULL; FILE* dst = NULL;
#ifdef _WIN32
        fopen_s(&src, filepath, "rb");
        fopen_s(&dst, bak, "wb");
#else
        src = fopen(filepath, "rb");
        dst = fopen(bak, "wb");
#endif
        if(src && dst) {
            char buf[8192]; size_t r;
            while((r=fread(buf,1,sizeof(buf),src))>0) fwrite(buf,1,r,dst);
        }
        if(src)
            fclose(src);
        if(dst)
            fclose(dst);
    }
    FILE* f = NULL;
#ifdef _WIN32
    fopen_s(&f, filepath, "wb");
#else
    f = fopen(filepath, "wb");
#endif
    if(!f) {
        if(errbuf && errsz) snprintf(errbuf, errsz, "cannot open for write");
        return 0;
    }
    save_node(root, f);
    fclose(f);
    return 1;
}

OTUINode* otui_node_add_child(OTUINode* parent, const char* name) {
    if(!parent) return NULL;
    OTUINode* n = node_new(name, parent->indent + 2);
    node_add_child(parent, n);
    return n;
}

int otui_node_remove_child(OTUINode* parent, OTUINode* child) {
    if(!parent || !child) return 0;
    for(size_t i=0;i<parent->nchildren;i++) {
        if(parent->children[i] == child) {
            // liberar nó
            otui_free(child);
            // compactar array
            for(size_t j=i+1;j<parent->nchildren;j++) parent->children[j-1] = parent->children[j];
            parent->nchildren--;
            return 1;
        }
    }
    return 0;
}

int otui_node_detach_child(OTUINode* parent, OTUINode* child, size_t* outIndex) {
    if(!parent || !child) return 0;
    for(size_t i=0;i<parent->nchildren;i++) {
        if(parent->children[i] == child) {
            if(outIndex) *outIndex = i;
            for(size_t j=i+1;j<parent->nchildren;j++) parent->children[j-1] = parent->children[j];
            parent->nchildren--;
            return 1;
        }
    }
    return 0;
}

int otui_node_insert_child(OTUINode* parent, OTUINode* child, size_t index) {
    if(!parent || !child) return 0;
    if(index > parent->nchildren) index = parent->nchildren;
    if(parent->nchildren == parent->cchildren) {
        size_t nc = parent->cchildren ? parent->cchildren * 2 : 4;
        OTUINode** nd = (OTUINode**)realloc(parent->children, nc * sizeof(OTUINode*));
        if(!nd) return 0;
        parent->children = nd; parent->cchildren = nc;
    }
    for(size_t j=parent->nchildren; j>index; --j) parent->children[j] = parent->children[j-1];
    parent->children[index] = child;
    parent->nchildren++;
    return 1;
}

int otui_node_bring_forward(OTUINode* parent, OTUINode* child) {
    if(!parent || !child || parent->nchildren < 2) return 0;
    size_t idx = (size_t)-1;
    for(size_t i=0; i<parent->nchildren; i++) {
        if(parent->children[i] == child) { idx=i; break; }
    }
    if(idx==(size_t)-1 || idx >= parent->nchildren-1) return 0;
    OTUINode* tmp = parent->children[idx];
    parent->children[idx] = parent->children[idx+1];
    parent->children[idx+1] = tmp;
    return 1;
}

int otui_node_send_backward(OTUINode* parent, OTUINode* child) {
    if(!parent || !child || parent->nchildren < 2) return 0;
    size_t idx = (size_t)-1;
    for(size_t i=0; i<parent->nchildren; i++) {
        if(parent->children[i] == child) { idx=i; break; }
    }
    if(idx==(size_t)-1 || idx==0) return 0;
    OTUINode* tmp = parent->children[idx];
    parent->children[idx] = parent->children[idx-1];
    parent->children[idx-1] = tmp;
    return 1;
}

OTUINode* otui_find_node(OTUINode* root, const char* name) {
    if(!root || !name) return NULL;
    if(strcmp(root->name, name) == 0) return root;
    
    for(size_t i = 0; i < root->nchildren; i++) {
        OTUINode* found = otui_find_node(root->children[i], name);
        if(found) return found;
    }
    
    return NULL;
}

void otui_resolve_inheritance(OTUINode* node, OTUINode* root) {
    if(!node || !node->base_style || !root) return;
    
    OTUINode* base = otui_find_node(root, node->base_style);
    if(!base) return;
    
    otui_resolve_inheritance(base, root);
    
    for(size_t i = 0; i < base->nprops; i++) {
        const char* key = base->props[i].key;
        if(!otui_prop_get(node, key)) {
            node_add_prop(node, key, base->props[i].value);
        }
    }
}

void otui_resolve_all_inheritance(OTUINode* root) {
    if(!root) return;
    
    for(size_t i = 0; i < root->nchildren; i++) {
        OTUINode* child = root->children[i];
        if(child->base_style) {
            otui_resolve_inheritance(child, root);
        }
        otui_resolve_all_inheritance(child);
    }
}

OTUIState* otui_state_get(const OTUINode* node, const char* condition) {
    if(!node || !condition) return NULL;
    for(size_t i = 0; i < node->nstates; i++) {
        if(strcmp(node->states[i].condition, condition) == 0) {
            return &node->states[i];
        }
    }
    return NULL;
}

const char* otui_state_prop_get(const OTUIState* state, const char* key) {
    if(!state || !key) return NULL;
    for(size_t i = 0; i < state->nprops; i++) {
        if(strcmp(state->props[i].key, key) == 0) {
            return state->props[i].value;
        }
    }
    return NULL;
}

OTUIEvent* otui_event_get(const OTUINode* node, const char* name) {
    if(!node || !name) return NULL;
    for(size_t i = 0; i < node->nevents; i++) {
        if(strcmp(node->events[i].name, name) == 0) {
            return &node->events[i];
        }
    }
    return NULL;
}
