/*
 * Copyright © 2011 Intel Corp.
 *
 * This  library is free  software; you can  redistribute it and/or
 * modify it  under  the terms  of the  GNU Lesser  General  Public
 * License  as published  by the Free  Software  Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed  in the hope that it will be useful,
 * but  WITHOUT ANY WARRANTY; without even  the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Authored by: Tomas Frydrych <tf@linux.intel.com>
 */

#include "config.h"

#include <string.h>
#include <rest/rest-xml-parser.h>

#include "yts-metadata-internal.h"
#include "yts-message.h"

static void yts_metadata_dispose (GObject *object);
static void yts_metadata_finalize (GObject *object);
static void yts_metadata_constructed (GObject *object);
static void yts_metadata_get_property (GObject    *object,
                                        guint       property_id,
                                        GValue     *value,
                                        GParamSpec *pspec);
static void yts_metadata_set_property (GObject      *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec   *pspec);

G_DEFINE_TYPE (YtsMetadata, yts_metadata, G_TYPE_OBJECT);

#define YTS_METADATA_GET_PRIVATE(o) \
(G_TYPE_INSTANCE_GET_PRIVATE ((o), YTS_TYPE_METADATA, YtsMetadataPrivate))

struct _YtsMetadataPrivate
{
  RestXmlNode  *top_level_node;
  char         *xml;
  char        **attributes;

  guint disposed : 1;
  guint readonly : 1;
};

enum
{
  N_SIGNALS,
};

enum
{
  PROP_0,
  PROP_XML,
  PROP_TOP_LEVEL_NODE,
  PROP_ATTRIBUTES,
};

static void
yts_metadata_class_init (YtsMetadataClass *klass)
{
  GParamSpec   *pspec;
  GObjectClass *object_class = (GObjectClass *)klass;

  g_type_class_add_private (klass, sizeof (YtsMetadataPrivate));

  object_class->dispose      = yts_metadata_dispose;
  object_class->finalize     = yts_metadata_finalize;
  object_class->constructed  = yts_metadata_constructed;
  object_class->get_property = yts_metadata_get_property;
  object_class->set_property = yts_metadata_set_property;

  /**
   * YtsMetadata:xml:
   *
   * The XML node this #YtsMetadata object is to represent
   *
   * This property is only valid during construction, as no copies are made.
   */
  pspec = g_param_spec_string ("xml",
                               "Metadata XML",
                               "Metadata XML",
                               NULL,
                               G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_XML, pspec);

  /**
   * YtsMetadata:top-level-node:
   *
   * The top level node for the metadata
   */
  pspec = g_param_spec_boxed ("top-level-node",
                              "Top-level node",
                              "Top-level RestXmlNode",
                              REST_TYPE_XML_NODE,
                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_TOP_LEVEL_NODE, pspec);

  /**
   * YtsMetadata:attributes:
   *
   * Top level attributes; this property is only valid during the construction
   * of the object, as no copies of the supplied value are made!
   */
  pspec = g_param_spec_boxed ("attributes",
                              "Top-level attributes",
                              "Top-level attributes",
                              G_TYPE_STRV,
                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (object_class, PROP_ATTRIBUTES, pspec);
}

static void
yts_metadata_constructed (GObject *object)
{
  YtsMetadata        *self = (YtsMetadata*) object;
  YtsMetadataPrivate *priv = self->priv;

  if (G_OBJECT_CLASS (yts_metadata_parent_class)->constructed)
    G_OBJECT_CLASS (yts_metadata_parent_class)->constructed (object);

  g_assert (priv->xml || priv->top_level_node);

  if (!priv->top_level_node && priv->xml)
    {

      RestXmlParser *parser = rest_xml_parser_new ();

      priv->top_level_node =
        rest_xml_parser_parse_from_data (parser, priv->xml, strlen (priv->xml));

      g_object_unref (parser);

      g_assert (priv->top_level_node);

      g_free (priv->xml);
      priv->xml = NULL;
    }

  if (priv->attributes)
    {
      char  **p;

      for (p = priv->attributes; *p && *(p + 1); p += 2)
        {
          const char *a = *p;
          const char *v = *(p + 1);

          rest_xml_node_add_attr (priv->top_level_node, a, v);
        }

      /*
       * the stored pointer is only valid during construction during the
       * construction
       */
      g_strfreev (priv->attributes);
      priv->attributes = NULL;
    }
}

static void
yts_metadata_get_property (GObject    *object,
                            guint       property_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
yts_metadata_set_property (GObject      *object,
                            guint         property_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  YtsMetadata        *self = (YtsMetadata*) object;
  YtsMetadataPrivate *priv = self->priv;

  switch (property_id)
    {
    case PROP_XML:
      priv->xml = g_value_dup_string (value);
      break;
    case PROP_TOP_LEVEL_NODE:
      if (priv->top_level_node)
        rest_xml_node_unref (priv->top_level_node);

      priv->top_level_node = g_value_get_boxed (value);
      break;
    case PROP_ATTRIBUTES:
      priv->attributes = g_value_dup_boxed (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
yts_metadata_init (YtsMetadata *self)
{
  self->priv = YTS_METADATA_GET_PRIVATE (self);
}

static void
yts_metadata_dispose (GObject *object)
{
  YtsMetadata        *self = (YtsMetadata*) object;
  YtsMetadataPrivate *priv = self->priv;

  if (priv->disposed)
    return;

  priv->disposed = TRUE;

  if (priv->top_level_node)
    rest_xml_node_unref (priv->top_level_node);

  G_OBJECT_CLASS (yts_metadata_parent_class)->dispose (object);
}

static void
yts_metadata_finalize (GObject *object)
{
  G_OBJECT_CLASS (yts_metadata_parent_class)->finalize (object);
}

/**
 * yts_metadata_get_root_node:
 * @self: #YtsMetadata
 *
 * Hands back pointer to the top-level node of the metadata, that can be used
 * with the #RestXmlNode API.
 *
 * NB: Any strings set directly through librest API must be in utf-8 encoding.
 *
 * Returns: (transfer none): #RestXmlNode representing the top-level node
 * of the metadata xml.
 */
RestXmlNode *
yts_metadata_get_root_node (YtsMetadata *self)
{
  YtsMetadataPrivate *priv;

  g_return_val_if_fail (YTS_IS_METADATA (self), NULL);

  priv = self->priv;

  return priv->top_level_node;
}

/*
 * yts_metadata_new_from_xml:
 * @xml: the xml the metatdata object is to represent
 *
 * Constructs a new #YtsMetadata object from the xml snippet.
 */
YtsMetadata *
yts_metadata_new_from_xml (const char *xml)
{
  YtsMetadata  *mdata;
  RestXmlParser *parser;
  RestXmlNode   *node;

  g_return_val_if_fail (xml && *xml, NULL);

  parser = rest_xml_parser_new ();

  node = rest_xml_parser_parse_from_data (parser, xml, strlen (xml));

  g_object_unref (parser);

  /* We do not unref the node, the object takes over the reference */

  mdata = yts_metadata_new_from_node (node, NULL);
  mdata->priv->readonly = TRUE;

  return mdata;
}

/*
 * yts_metadata_new_from_node:
 * @node: #RestXmlNode
 * @attributes: %NULL terminated array of name/value pairs for additional
 * attributes, can be %NULL
 *
 * Private constructor.
 *
 * Returns: (transfer full): newly allocated #YtsMetadata subclass.
 */
YtsMetadata *
yts_metadata_new_from_node (RestXmlNode       *node,
                            char const *const *attributes)
{
  YtsMetadata  *mdata = NULL;

  g_return_val_if_fail (node && node->name, NULL);

  if (!g_strcmp0 (node->name, "message"))
    {
      if (attributes)
        mdata = g_object_new (YTS_TYPE_MESSAGE,
                              "top-level-node", node,
                              "attributes",     attributes,
                              NULL);
      else
        mdata = g_object_new (YTS_TYPE_MESSAGE, "top-level-node", node, NULL);
    }
  else
    g_warning ("Unknown top level node '%s'", node->name);

  /* We do not unref the node, the object takes over the reference */

  return mdata;
}

/**
 * yts_metadata_get_attribute:
 * @self: #YtsMetadata
 * @name: name of the attribute to look up
 *
 * Retrieves the value of an attribute of the given name on the top level node
 * of the #YtsMetadata object (to query attributes on children of the top level
 * node, you need to use yts_metadata_get_root_node() and the librest API to
 * locate and query the appropriate node).
 *
 * Returns: (transfer none): the attribute value or %NULL if attribute
 * does not exist.
 */
const char *
yts_metadata_get_attribute (YtsMetadata *self, const char *name)
{
  YtsMetadataPrivate *priv;

  g_return_val_if_fail (YTS_IS_METADATA (self), NULL);

  priv = self->priv;

  g_return_val_if_fail (priv->top_level_node, NULL);

  return rest_xml_node_get_attr (priv->top_level_node, name);
}

/**
 * yts_metadata_add_attribute:
 * @self: #YtsMetadata
 * @name: name of the attribute to add
 * @value: value of the attribute to add
 *
 * Adds an attribute of the given name on the top level node
 * of the #YtsMetadata object (to add attributes to children of the top level
 * node, you need to use yts_metadata_get_root_node() and the librest API to
 * construct the metadata tree).
 *
 * NB: Both attribute names and values must be in utf-8 encoding.
 */
void
yts_metadata_add_attribute (YtsMetadata *self,
                             const char   *name,
                             const char   *value)
{
  YtsMetadataPrivate *priv;

  g_return_if_fail (YTS_IS_METADATA (self) && name && *name && value);

  priv = self->priv;

  g_return_if_fail (priv->top_level_node);
  g_return_if_fail (!priv->readonly);

  rest_xml_node_add_attr (priv->top_level_node, name, value);
}

/**
 * yts_metadata_to_string:
 * @self: #YtsMetadata
 *
 * Converts the #YtsMetada object in XML representation.
 *
 * Returns: (transfer full): xml string; the caller must free the string
 * with g_free() when no longer needed.
 */
char *
yts_metadata_to_string (YtsMetadata *self)
{
  YtsMetadataPrivate *priv;

  g_return_val_if_fail (YTS_IS_METADATA (self), NULL);

  priv = self->priv;

  g_return_val_if_fail (priv->top_level_node, NULL);

  return rest_xml_node_print (priv->top_level_node);
}

/*
 * yts_metadata_extract:
 * @self: #YtsMetadata
 * @body: location to store the body of the metadata; free with g_free() when no
 * longer needed.
 *
 * Extracts the top level attributes into a hash table, and converts the
 * content of any child nodes to xml string. This is intended for use by
 * #YtsClient when sending messages.
 *
 * Returns: (transfer full): top level attributes, the caller holds a
 * reference on the returned hash table, which it needs to release with
 * g_hash_table_unref() when no longer needed.
 */
GHashTable *
yts_metadata_extract (YtsMetadata *self, char **body)
{
  YtsMetadataPrivate *priv;
  RestXmlNode         *n0;
  GHashTableIter       iter;
  gpointer             key, value;
  char                *b;

  g_return_val_if_fail (YTS_IS_METADATA (self) && body, NULL);

  priv = self->priv;
  n0 = priv->top_level_node;

  b = g_strdup (n0->content);

  /*
   * This is necessary for the g_strconcat() below to work */
  if (!b)
    b = g_strdup ("");

  g_hash_table_iter_init (&iter, n0->children);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      char *child = rest_xml_node_print ((RestXmlNode *) value);

      b = g_strconcat (b, child, NULL);
      g_free (child);
    }

  *body = b;

  return g_hash_table_ref (n0->attrs);
}

static gboolean
yts_rest_xml_node_check_attrs (RestXmlNode *node0, RestXmlNode *node1)
{
  GHashTableIter iter;
  gpointer       key, value;

  if (g_hash_table_size (node0->attrs) != g_hash_table_size (node1->attrs))
    return FALSE;

  g_hash_table_iter_init (&iter, node0->attrs);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const char *at1 = rest_xml_node_get_attr (node1, key);

      if (!value && !at1)
        continue;

      if ((!at1 && value) || (at1 && !value) || g_strcmp0 (value, at1))
        return FALSE;
    }

  return TRUE;
}

static gboolean yts_rest_xml_node_check_children (RestXmlNode*, RestXmlNode*);

/*
 * NB: this function is somewhat simplistic; it assumes that if two nodes have
 *     siblings, the corresponding siblings must be in identical order. But
 *     can we define equaly in any other way?
 */
static gboolean
yts_rest_xml_node_check_siblings (RestXmlNode *node0, RestXmlNode *node1)
{
  RestXmlNode *sib0 = node0->next;
  RestXmlNode *sib1 = node1->next;

  if (!sib0 && !sib1)
    return TRUE;

  do
    {
      if ((!sib0 && sib1) || (sib0 && !sib1))
        return FALSE;

      if (!yts_rest_xml_node_check_attrs (sib0, sib1))
        return FALSE;

      if (!yts_rest_xml_node_check_children (sib0, sib1))
        return FALSE;

      sib0 = sib0->next;
      sib1 = sib1->next;
    } while (sib0 || sib1);

  return TRUE;
}

static gboolean
yts_rest_xml_node_check_children (RestXmlNode *node0, RestXmlNode *node1)
{
  GHashTableIter iter;
  gpointer       key, value;

  if (g_hash_table_size (node0->attrs) != g_hash_table_size (node1->attrs))
    return FALSE;

  g_hash_table_iter_init (&iter, node0->children);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const char  *name0  = key;
      RestXmlNode *child0 = value;
      RestXmlNode *child1 = rest_xml_node_find (node1, name0);

      if (!child1 ||
          (child0->next && !child1->next) || (!child0->next && child1->next))
        return FALSE;

      if (!yts_rest_xml_node_check_attrs (child0, child1))
        return FALSE;

      if (!yts_rest_xml_node_check_siblings (child0, child1))
        return FALSE;

      if (!yts_rest_xml_node_check_children (child0, child1))
        return FALSE;
    }

  return TRUE;
}

/**
 * yts_metadata_is_equal:
 * @self: #YtsMetadata,
 * @other: #YtsMetadata
 *
 * Compares two metadata instances and returns %TRUE if they are equal.
 * NB: equality implies identity of type, i.e., different subclasses will
 * always be unequal.
 *
 * Returns: %TRUE if equal, %FALSE otherwise.
 */
gboolean
yts_metadata_is_equal (YtsMetadata *self, YtsMetadata *other)
{
  RestXmlNode *node0;
  RestXmlNode *node1;

  g_return_val_if_fail (YTS_IS_METADATA (self) && YTS_IS_METADATA (other),
                        FALSE);

  if (G_OBJECT_TYPE (self) != G_OBJECT_TYPE (other))
    return FALSE;

  node0 = yts_metadata_get_root_node ((YtsMetadata*) self);
  node1 = yts_metadata_get_root_node ((YtsMetadata*) other);

  if (!yts_rest_xml_node_check_attrs (node0, node1))
    return FALSE;

  if (!yts_rest_xml_node_check_children (node0, node1))
    return FALSE;

  return TRUE;
}
