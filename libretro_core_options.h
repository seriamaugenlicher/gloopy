#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>

#include <libretro.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
*/

struct retro_core_option_v2_category option_cats_us[] = {
   {
      "video",
      "Video",
      "Configure display options."
   },
   {
      "audio",
      "Audio",
      "Configure audio options."
   },
   {
      "peripheral",
      "Peripherals",
      "Controller, Loopy Mouse, and seal printer."
   },
   {
      "performance",
      "Performance",
      "Speed-up options for slower hardware."
   },
   { NULL, NULL, NULL },
};

struct retro_core_option_v2_definition option_defs_us[] = {
   /* Video */
   {
      "loopy_crop_overscan",
      "Video > Crop Overscan",
      "Crop Overscan",
      "Crop the output to the visible scanline count set by the game. When disabled, the full 240-line frame is always shown.",
      NULL,
      "video",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },

   /* Audio */
   {
      "loopy_mix_level",
      "Audio > Synth Mix Level",
      "Synth Mix Level",
      "Output level of the sound synth. The default matches the level measured on real hardware.",
      NULL,
      "audio",
      {
         { "0.55", "0.55 (quiet)" },
         { "0.62", "0.62 (hardware level)" },
         { "0.80", "0.80 (loud, may clip)" },
         { NULL, NULL },
      },
      "0.62"
   },

   /* Peripherals */
   {
      "loopy_input_device",
      "Peripherals > Input Device",
      "Input Device",
      "The Loopy has a single controller port, so a gamepad and a Loopy Mouse cannot be plugged in at the same time. However, Gloopy can simulate using both at once.",
      NULL,
      "peripheral",
      {
         { "auto",       "Controller + Mouse" },
         { "controller", "Controller only" },
         { "mouse",      "Mouse only" },
         { NULL, NULL },
      },
      "auto"
   },
   {
      "loopy_mouse_sensitivity",
      "Peripherals > Mouse Sensitivity",
      "Mouse Sensitivity",
      "1x is tuned to the feel of the Loopy Mouse's low-resolution ball movement.",
      NULL,
      "peripheral",
      {
         { "0.25", "0.25x (much slower)" },
         { "0.50", "0.5x (slower)" },
         { "1.00", "1x (default)" },
         { "2.00", "2x (faster)" },
         { "4.00", "4x (much faster)" },
         { NULL, NULL },
      },
      "1.00"
   },

   {
      "loopy_seal_format",
      "Peripherals > Seal Sticker Format",
      "Seal Sticker Format",
      "The image format printed seals are saved in. PNG suits them: a seal is flat-coloured pixel art, which PNG stores exactly and compresses to a fraction of the size, and it is what frontends save screenshots as. BMP is uncompressed and offered for anything that cannot read a PNG.",
      NULL,
      "peripheral",
      {
         { "png", "PNG" },
         { "bmp", "BMP" },
         { NULL, NULL },
      },
      "png"
   },
   {
      "loopy_printer",
      "Peripherals > Seal Printer",
      "Seal Printer",
      "Emulates the Loopy's built-in seal (sticker) printer. Prints are saved as BMP images in the frontend's save directory. Requires restart.",
      NULL,
      "peripheral",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },

   /* Performance */
   {
      "loopy_idle_skip",
      "Performance > Idle Loop Skip",
      "Idle Loop Skip",
      "LEAVE ENABLED. Most Loopy games spend over 95% of their CPU cycles in an idle loop. This detects those loops and fast-forwards through them, making the core roughly 2-3x faster. For troubleshooting only.",
      NULL,
      "performance",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      "loopy_frameskip",
      "Performance > Frameskip",
      "Frameskip",
      "For hardware too slow to draw all 60 frames per second. Leave disabled unless the core is running below full speed.",
      NULL,
      "performance",
      {
         { "disabled", "disabled" },
         { "auto",     "auto (up to 3 frames)" },
         { "1",        "fixed: skip 1 of every 2" },
         { "3",        "fixed: skip 3 of every 4" },
         { NULL, NULL },
      },
      "disabled"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};

struct retro_core_options_v2 options_us = {
   option_cats_us,
   option_defs_us
};

/*
 ********************************
 * Functions
 ********************************
*/

/* Handles configuration/setting of core options.
 * Should only be called inside retro_set_environment().
 */
static inline void libretro_set_core_options(retro_environment_t environ_cb)
{
   unsigned version = 0;

   if (!environ_cb)
      return;

   if (environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && (version >= 2))
   {
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &options_us);
   }
   else
   {
      size_t i, num_options = 0;
      struct retro_variable *variables = NULL;
      char **values_buf = NULL;

      /* Determine number of options */
      while (true)
      {
         if (!option_defs_us[num_options].key)
            break;
         num_options++;
      }

      /* Allocate arrays */
      variables  = (struct retro_variable *)calloc(num_options + 1, sizeof(struct retro_variable));
      values_buf = (char **)calloc(num_options, sizeof(char *));

      if (!variables || !values_buf)
         goto error;

      /* Copy parameters from option_defs_us array */
      for (i = 0; i < num_options; i++)
      {
         const char *key                        = option_defs_us[i].key;
         const char *desc                       = option_defs_us[i].desc;
         const char *default_value              = option_defs_us[i].default_value;
         struct retro_core_option_value *values = option_defs_us[i].values;
         size_t buf_len                         = 3;
         size_t default_index                   = 0;

         values_buf[i] = NULL;

         if (desc)
         {
            size_t num_values = 0;

            /* Determine number of values */
            while (true)
            {
               if (!values[num_values].value)
                  break;

               /* Check if this is the default value */
               if (default_value)
                  if (strcmp(values[num_values].value, default_value) == 0)
                     default_index = num_values;

               buf_len += strlen(values[num_values].value);
               num_values++;
            }

            /* Build values string */
            if (num_values > 0)
            {
               size_t j;

               buf_len += num_values - 1;
               buf_len += strlen(desc);

               values_buf[i] = (char *)calloc(buf_len, sizeof(char));
               if (!values_buf[i])
                  goto error;

               strcpy(values_buf[i], desc);
               strcat(values_buf[i], "; ");

               /* Default value goes first */
               strcat(values_buf[i], values[default_index].value);

               /* Add remaining values */
               for (j = 0; j < num_values; j++)
               {
                  if (j != default_index)
                  {
                     strcat(values_buf[i], "|");
                     strcat(values_buf[i], values[j].value);
                  }
               }
            }
         }

         variables[i].key   = key;
         variables[i].value = values_buf[i];
      }

      /* Set variables */
      environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);

error:
      /* Clean up */
      if (values_buf)
      {
         for (i = 0; i < num_options; i++)
         {
            if (values_buf[i])
            {
               free(values_buf[i]);
               values_buf[i] = NULL;
            }
         }

         free(values_buf);
         values_buf = NULL;
      }

      if (variables)
      {
         free(variables);
         variables = NULL;
      }
   }
}

#ifdef __cplusplus
}
#endif

#endif
