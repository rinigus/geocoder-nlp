select f.*, o.name from 
(select o.name, parent, postal_code, t.name as type_name, count(*) as cnt from object_primary o
join "type" t on t.id = o.type_id
group by o.name, parent, postal_code, t.name
order by cnt desc
limit 25) f 
join object_primary o on f.parent=o.id
